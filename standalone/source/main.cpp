#define SDL_MAIN_HANDLED
#include <switch.h>
#include <SDL.h>
#include <libretro.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <strings.h>
#include <vector>
#include <sys/stat.h>

#define RETRO_DEVICE_GAMEPAD RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0)
#define RETRO_DEVICE_ZAPPER  RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_POINTER, 0)

extern "C" {
void retro_set_environment(retro_environment_t);
void retro_set_video_refresh(retro_video_refresh_t);
void retro_set_audio_sample(retro_audio_sample_t);
void retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void retro_set_input_poll(retro_input_poll_t);
void retro_set_input_state(retro_input_state_t);
void retro_init(void);
void retro_deinit(void);
bool retro_load_game(const retro_game_info*);
void retro_unload_game(void);
void retro_run(void);
void retro_set_controller_port_device(unsigned, unsigned);
};

static const char* const APP_DIR = "sdmc:/switch/gyroduckhunt";
static const char* const ROM_DIR = "sdmc:/switch/gyroduckhunt/roms";
static const char* const SAVE_DIR = "sdmc:/switch/gyroduckhunt/saves";
static const int SCREEN_W = 1280;
static const int SCREEN_H = 720;
static const int GAME_W = 256;
static const int GAME_H = 240;
static const float GYRO_GAIN = 10430.0f;
static const unsigned MAX_ROMS = 256;

static SDL_Window* window_handle = nullptr;
static SDL_Renderer* renderer = nullptr;
static SDL_Texture* game_texture = nullptr;
static SDL_AudioDeviceID audio_device = 0;
static PadState pads[2];
static HidSixAxisSensorHandle gyro_handle;
static bool gyro_ready = false;
static bool game_loaded = false;
static bool frontend_ready = false;
static bool crosshair_enabled = true;
static int cursor_x = 32767;
static int cursor_y = 32767;
static int cursor_dx = 0;
static int cursor_dy = 0;
static int32_t viewport_x = 160;
static int32_t viewport_y = 0;
static int32_t viewport_w = 960;
static int32_t viewport_h = 720;
static std::vector<std::string> roms;
static int selected_rom = 0;
static std::vector<uint8_t> rom_data;
static std::string loaded_path;
static uint64_t pad0_buttons = 0;
static uint64_t right_buttons = 0;
static HidAnalogStickState left_stick = {};
static unsigned core_width = GAME_W;
static unsigned core_height = GAME_H;
static enum retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_XRGB8888;
static std::string status_line;

static int16_t pointer_axis(int value)
{
   value = std::max(-32767, std::min(32767, value));
   /* Nestopia treats exactly zero as "keep the previous crosshair position". */
   return (int16_t)(value == 0 ? 1 : value);
}

static void log_printf(enum retro_log_level, const char* fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

static bool has_extension(const char* name)
{
   const char* dot = strrchr(name, '.');
   if (!dot)
      return false;
   std::string ext(dot);
   std::transform(ext.begin(), ext.end(), ext.begin(),
         [](unsigned char c) { return (char)std::tolower(c); });
   return ext == ".nes" || ext == ".unf" || ext == ".unif" ||
      ext == ".fds" || ext == ".nsf";
}

static void scan_roms(void)
{
   DIR* dir;
   struct dirent* entry;
   roms.clear();
   mkdir(APP_DIR, 0777);
   mkdir(ROM_DIR, 0777);
   mkdir(SAVE_DIR, 0777);
   dir = opendir(ROM_DIR);
   if (!dir)
      return;
   while ((entry = readdir(dir)) != nullptr && roms.size() < MAX_ROMS)
      if (entry->d_type != DT_DIR && has_extension(entry->d_name))
         roms.emplace_back(entry->d_name);
   closedir(dir);
   std::sort(roms.begin(), roms.end(),
         [](const std::string& a, const std::string& b) {
            return strcasecmp(a.c_str(), b.c_str()) < 0;
         });
   if (selected_rom >= (int)roms.size())
      selected_rom = roms.empty() ? 0 : (int)roms.size() - 1;
}

static bool read_rom(const std::string& path)
{
   FILE* f = fopen(path.c_str(), "rb");
   long size;
   if (!f)
      return false;
   if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0)
   {
      fclose(f);
      return false;
   }
   rom_data.resize((size_t)size);
   if (fread(rom_data.data(), 1, rom_data.size(), f) != rom_data.size())
   {
      fclose(f);
      rom_data.clear();
      return false;
   }
   fclose(f);
   return true;
}

static bool load_selected_rom(void)
{
   retro_game_info info;
   if (selected_rom < 0 || selected_rom >= (int)roms.size())
      return false;
   loaded_path = std::string(ROM_DIR) + "/" + roms[selected_rom];
   if (!read_rom(loaded_path))
   {
      status_line = "Cannot read selected ROM";
      return false;
   }
   memset(&info, 0, sizeof(info));
   info.path = loaded_path.c_str();
   info.data = rom_data.data();
   info.size = rom_data.size();
   if (!retro_load_game(&info))
   {
      rom_data.clear();
      status_line = "Nestopia rejected this ROM";
      return false;
   }
   retro_set_controller_port_device(0, RETRO_DEVICE_GAMEPAD);
   retro_set_controller_port_device(1, RETRO_DEVICE_ZAPPER);
   game_loaded = true;
   status_line.clear();
   return true;
}

static bool environment(unsigned cmd, void* data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_GET_CAN_DUPE:
         *(bool*)data = true;
         return true;
      case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
         *(unsigned*)data = 2;
         return true;
      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
         ((retro_log_callback*)data)->log = log_printf;
         return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         retro_variable* var = (retro_variable*)data;
         if (!strcmp(var->key, "nestopia_zapper_device"))
            var->value = "pointer";
         else if (!strcmp(var->key, "nestopia_show_crosshair"))
            var->value = crosshair_enabled ? "enabled" : "disabled";
         else
            var->value = nullptr;
         return var->value != nullptr;
      }
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
         *(bool*)data = false;
         return true;
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
         *(const char**)data = APP_DIR;
         return true;
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char**)data = SAVE_DIR;
         return true;
      case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
         *(const char**)data = ROM_DIR;
         return true;
      case RETRO_ENVIRONMENT_GET_LANGUAGE:
         *(unsigned*)data = RETRO_LANGUAGE_ENGLISH;
         return true;
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
         pixel_format = *(enum retro_pixel_format*)data;
         return pixel_format == RETRO_PIXEL_FORMAT_XRGB8888;
      case RETRO_ENVIRONMENT_SET_GEOMETRY:
      {
         const struct retro_game_geometry* g = (const struct retro_game_geometry*)data;
         core_width = g->base_width;
         core_height = g->base_height;
         return true;
      }
      case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
         return true;
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
      case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
      case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
         return true;
      default:
         return false;
   }
}

static void input_poll(void) {}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
   unsigned base_device = device & RETRO_DEVICE_MASK;
   if (base_device == RETRO_DEVICE_JOYPAD && port == 0)
   {
      switch (id)
      {
         case RETRO_DEVICE_ID_JOYPAD_B:      return (pad0_buttons & HidNpadButton_L) != 0;
         case RETRO_DEVICE_ID_JOYPAD_Y:      return (pad0_buttons & HidNpadButton_ZL) != 0;
         case RETRO_DEVICE_ID_JOYPAD_SELECT: return (pad0_buttons & HidNpadButton_LeftSL) != 0;
         case RETRO_DEVICE_ID_JOYPAD_START:  return (pad0_buttons & HidNpadButton_StickL) != 0;
         case RETRO_DEVICE_ID_JOYPAD_UP:     return (pad0_buttons & HidNpadButton_Up) != 0 || left_stick.y > 12000;
         case RETRO_DEVICE_ID_JOYPAD_DOWN:   return (pad0_buttons & HidNpadButton_Down) != 0 || left_stick.y < -12000;
         case RETRO_DEVICE_ID_JOYPAD_LEFT:   return (pad0_buttons & HidNpadButton_Left) != 0 || left_stick.x < -12000;
         case RETRO_DEVICE_ID_JOYPAD_RIGHT:  return (pad0_buttons & HidNpadButton_Right) != 0 || left_stick.x > 12000;
         case RETRO_DEVICE_ID_JOYPAD_A:      return (pad0_buttons & HidNpadButton_R) != 0;
         case RETRO_DEVICE_ID_JOYPAD_X:      return (pad0_buttons & HidNpadButton_LeftSL) != 0;
         case RETRO_DEVICE_ID_JOYPAD_L:      return (pad0_buttons & HidNpadButton_L) != 0;
         case RETRO_DEVICE_ID_JOYPAD_L2:     return (pad0_buttons & HidNpadButton_ZL) != 0;
         case RETRO_DEVICE_ID_JOYPAD_L3:     return (pad0_buttons & HidNpadButton_StickL) != 0;
      }
      return 0;
   }
   if (base_device == RETRO_DEVICE_ANALOG && port == 0 &&
         index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
   {
      if (id == RETRO_DEVICE_ID_ANALOG_X)
         return (int16_t)std::max(-32767, std::min(32767, left_stick.x));
      if (id == RETRO_DEVICE_ID_ANALOG_Y)
         return (int16_t)-std::max(-32767, std::min(32767, left_stick.y));
   }
   if (port != 1)
      return 0;

   if (base_device == RETRO_DEVICE_POINTER)
   {
      switch (id)
      {
         case RETRO_DEVICE_ID_POINTER_X:       return pointer_axis(cursor_x - 32767);
         case RETRO_DEVICE_ID_POINTER_Y:       return pointer_axis(cursor_y - 32767);
         case RETRO_DEVICE_ID_POINTER_PRESSED: return (right_buttons & HidNpadButton_ZR) != 0;
      }
   }
   else if (base_device == RETRO_DEVICE_LIGHTGUN)
   {
      switch (id)
      {
         case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X: return pointer_axis(cursor_x - 32767);
         case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y: return pointer_axis(cursor_y - 32767);
         case RETRO_DEVICE_ID_LIGHTGUN_TRIGGER:  return (right_buttons & HidNpadButton_ZR) != 0;
         case RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN:
         case RETRO_DEVICE_ID_LIGHTGUN_RELOAD:   return 0;
      }
   }
   else if (base_device == RETRO_DEVICE_MOUSE)
   {
      switch (id)
      {
         case RETRO_DEVICE_ID_MOUSE_X:     { int v = cursor_dx; cursor_dx = 0; return (int16_t)v; }
         case RETRO_DEVICE_ID_MOUSE_Y:     { int v = cursor_dy; cursor_dy = 0; return (int16_t)v; }
         case RETRO_DEVICE_ID_MOUSE_LEFT:  return (right_buttons & HidNpadButton_ZR) != 0;
      }
   }
   return 0;
}

static void audio_sample(int16_t left, int16_t right)
{
   if (!audio_device)
      return;
   int16_t sample[2] = { left, right };
   if (SDL_GetQueuedAudioSize(audio_device) < 48000)
      SDL_QueueAudio(audio_device, sample, sizeof(sample));
}

static size_t audio_batch(const int16_t* data, size_t frames)
{
   if (audio_device && SDL_GetQueuedAudioSize(audio_device) < 48000)
      SDL_QueueAudio(audio_device, data, (Uint32)(frames * 2 * sizeof(int16_t)));
   return frames;
}

static void video_refresh(const void* data, unsigned width, unsigned height, size_t pitch)
{
   if (!data || !renderer || !game_texture || pixel_format != RETRO_PIXEL_FORMAT_XRGB8888)
      return;
   if (width != GAME_W || height > GAME_H)
   {
      SDL_DestroyTexture(game_texture);
      game_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888,
            SDL_TEXTUREACCESS_STREAMING, width, height);
      core_width = width;
      core_height = height;
   }
   SDL_UpdateTexture(game_texture, nullptr, data, (int)pitch);
   SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
   SDL_RenderClear(renderer);
   SDL_Rect dst = { viewport_x, viewport_y, viewport_w, viewport_h };
   SDL_RenderCopy(renderer, game_texture, nullptr, &dst);
   if (crosshair_enabled)
   {
      int x = viewport_x + (int)((int64_t)cursor_x * viewport_w / 65534);
      int y = viewport_y + (int)((int64_t)cursor_y * viewport_h / 65534);
      SDL_SetRenderDrawColor(renderer, 255, 48, 48, 255);
      SDL_RenderDrawLine(renderer, x - 12, y, x + 12, y);
      SDL_RenderDrawLine(renderer, x, y - 12, x, y + 12);
      SDL_Rect box = { x - 4, y - 4, 9, 9 };
      SDL_RenderDrawRect(renderer, &box);
   }
   SDL_RenderPresent(renderer);
}

static const uint8_t font[36][5] = {
   {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
   {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
   {0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
   {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},
   {0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},
   {0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
   {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},
   {0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},
   {0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
   {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},
   {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
   {0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},
   {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
   {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},
   {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
   {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
   {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
   {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e},
};

static void draw_text(int x, int y, const std::string& text, int scale, SDL_Color color)
{
   SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
   for (char raw : text)
   {
      char c = (char)std::toupper((unsigned char)raw);
      int idx = (c >= 'A' && c <= 'Z') ? c - 'A' :
         (c >= '0' && c <= '9') ? 26 + c - '0' : -1;
      if (idx >= 0)
      {
         for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
               if (font[idx][col] & (1 << row))
               {
                  SDL_Rect pixel = { x + col * scale, y + row * scale, scale, scale };
                  SDL_RenderFillRect(renderer, &pixel);
               }
      }
      else if (c == '-')
      {
         SDL_Rect pixel = { x, y + 3 * scale, 4 * scale, scale };
         SDL_RenderFillRect(renderer, &pixel);
      }
      else if (c == '.')
      {
         SDL_Rect pixel = { x + 2 * scale, y + 6 * scale, scale, scale };
         SDL_RenderFillRect(renderer, &pixel);
      }
      else if (c == '/')
      {
         for (int i = 0; i < 7; i++)
         {
            SDL_Rect pixel = { x + (4 - i / 2) * scale, y + i * scale, scale, scale };
            SDL_RenderFillRect(renderer, &pixel);
         }
      }
      x += 6 * scale;
   }
}

static std::string short_name(std::string name)
{
   size_t dot = name.find_last_of('.');
   if (dot != std::string::npos)
      name.resize(dot);
   if (name.size() > 28)
      name = name.substr(0, 25) + "...";
   return name;
}

static void render_menu(void)
{
   SDL_SetRenderDrawColor(renderer, 10, 14, 22, 255);
   SDL_RenderClear(renderer);
   SDL_Color white = { 240, 245, 255, 255 };
   SDL_Color cyan = { 80, 220, 255, 255 };
   draw_text(64, 48, "GYRO ZAPPER - SELECT NES ROM", 3, cyan);
   draw_text(64, 92, "A: PLAY   UP DOWN: SELECT", 2, white);
   draw_text(64, 124, "ZR: FIRE   PLUS + R STICK: RECENTER", 2, white);
   draw_text(64, 156, gyro_ready ? "RIGHT GYRO READY" : "RIGHT GYRO NOT FOUND", 2,
         gyro_ready ? cyan : SDL_Color{255, 100, 80, 255});
   if (roms.empty())
   {
      draw_text(64, 210, "NO ROMS FOUND", 3, white);
      draw_text(64, 254, "COPY NES FILES TO /SWITCH/GYRODUCKHUNT/ROMS", 2, cyan);
   }
   else
   {
      int first = std::max(0, selected_rom - 18);
      int last = std::min((int)roms.size(), first + 25);
      for (int i = first; i < last; i++)
      {
         int y = 190 + (i - first) * 19;
         if (i == selected_rom)
         {
            SDL_Rect highlight = { 48, y - 3, 850, 18 };
            SDL_SetRenderDrawColor(renderer, 36, 76, 120, 255);
            SDL_RenderFillRect(renderer, &highlight);
         }
         draw_text(64, y, short_name(roms[i]), 2, i == selected_rom ? cyan : white);
      }
   }
   if (!status_line.empty())
      draw_text(64, 680, status_line, 2, SDL_Color{255, 100, 80, 255});
   SDL_RenderPresent(renderer);
}

static void poll_switch_input(void)
{
   padUpdate(&pads[0]);
   padUpdate(&pads[1]);
   pad0_buttons = padGetButtons(&pads[0]);
      left_stick = padGetStickPos(&pads[0], 0);
   right_buttons = (uint64_t)padGetButtons(&pads[1]);

   if ((right_buttons & (HidNpadButton_Plus | HidNpadButton_StickR)) ==
         (HidNpadButton_Plus | HidNpadButton_StickR))
   {
      cursor_x = cursor_y = 32767;
      cursor_dx = cursor_dy = 0;
   }
   else if (gyro_ready)
   {
      HidSixAxisSensorState state;
      if (R_SUCCEEDED(hidGetSixAxisSensorStates(gyro_handle, &state, 1)) && state.delta_time)
      {
         float dt = std::min(0.05f, (float)state.delta_time / 1000000000.0f);
         float dx = -state.angular_velocity.y * dt * GYRO_GAIN;
         float dy =  state.angular_velocity.x * dt * GYRO_GAIN;
         int ix = (int)dx;
         int iy = (int)dy;
         cursor_x = std::max(0, std::min(65534, cursor_x + ix));
         cursor_y = std::max(0, std::min(65534, cursor_y + iy));
         cursor_dx += ix;
         cursor_dy += iy;
      }
   }
}

static void start_core(void)
{
   retro_set_environment(environment);
   retro_set_video_refresh(video_refresh);
   retro_set_audio_sample(audio_sample);
   retro_set_audio_sample_batch(audio_batch);
   retro_set_input_poll(input_poll);
   retro_set_input_state(input_state);
   retro_init();
   retro_set_controller_port_device(0, RETRO_DEVICE_GAMEPAD);
   retro_set_controller_port_device(1, RETRO_DEVICE_ZAPPER);
}

int main(int, char**)
{
   if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
      return 1;
   SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
   window_handle = SDL_CreateWindow("Gyro Zapper NES", SDL_WINDOWPOS_CENTERED,
         SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H, SDL_WINDOW_FULLSCREEN);
   if (!window_handle)
      return 2;
   renderer = SDL_CreateRenderer(window_handle, -1,
         SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
   if (!renderer)
      renderer = SDL_CreateRenderer(window_handle, -1, SDL_RENDERER_SOFTWARE);
   if (!renderer)
      return 3;
   game_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888,
         SDL_TEXTUREACCESS_STREAMING, GAME_W, GAME_H);
   SDL_AudioSpec want = {};
   want.freq = 48000;
   want.format = AUDIO_S16SYS;
   want.channels = 2;
   want.samples = 1024;
   audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
   if (audio_device)
      SDL_PauseAudioDevice(audio_device, 0);

   padConfigureInput(2, HidNpadStyleSet_NpadStandard);
   padInitializeDefault(&pads[0]);
   padInitialize(&pads[1], HidNpadIdType_No2);
   padUpdate(&pads[0]);
   padUpdate(&pads[1]);
   if (R_SUCCEEDED(hidGetSixAxisSensorHandles(&gyro_handle, 1,
               HidNpadIdType_No2, HidNpadStyleTag_NpadJoyRight)) &&
         R_SUCCEEDED(hidStartSixAxisSensor(gyro_handle)))
      gyro_ready = true;

   start_core();
   scan_roms();
   frontend_ready = true;
   bool running = true;
   uint64_t previous_buttons = 0;
   while (running && appletMainLoop())
   {
      poll_switch_input();
      uint64_t buttons = padGetButtons(&pads[0]);
      uint64_t pressed = buttons & ~previous_buttons;
      previous_buttons = buttons;

      if (!game_loaded)
      {
         if (!roms.empty() && (pressed & HidNpadButton_Up))
            selected_rom = std::max(0, selected_rom - 1);
         if (!roms.empty() && (pressed & HidNpadButton_Down))
            selected_rom = std::min((int)roms.size() - 1, selected_rom + 1);
         if ((pressed & HidNpadButton_A) && !roms.empty())
            load_selected_rom();
         if (pressed & HidNpadButton_Minus)
            scan_roms();
         if (pressed & HidNpadButton_Plus)
            running = false;
         if (roms.empty() || !game_loaded)
            render_menu();
      }
      else
      {
         if ((buttons & (HidNpadButton_Plus | HidNpadButton_StickL)) ==
               (HidNpadButton_Plus | HidNpadButton_StickL))
         {
            retro_unload_game();
            game_loaded = false;
            rom_data.clear();
            scan_roms();
            continue;
         }
         if (pressed & HidNpadButton_Minus)
         {
            retro_unload_game();
            game_loaded = false;
            rom_data.clear();
            scan_roms();
            continue;
         }
         retro_run();
         cursor_dx = cursor_dy = 0;
      }
   }
   if (game_loaded)
      retro_unload_game();
   if (frontend_ready)
      retro_deinit();
   if (gyro_ready)
      hidStopSixAxisSensor(gyro_handle);
   if (audio_device)
      SDL_CloseAudioDevice(audio_device);
   if (game_texture)
      SDL_DestroyTexture(game_texture);
   if (renderer)
      SDL_DestroyRenderer(renderer);
   if (window_handle)
      SDL_DestroyWindow(window_handle);
   SDL_Quit();
   return 0;
}

