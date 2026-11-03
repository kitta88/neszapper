#define SDL_MAIN_HANDLED
#include <switch.h>
#include <SDL.h>
#include <SDL_ttf.h>
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
#include <unordered_map>
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
static const char* const CONFIG_PATH = "sdmc:/switch/gyroduckhunt/config.ini";
static const int SCREEN_W = 1280;
static const int SCREEN_H = 720;
static const int GAME_W = 256;
static const int GAME_H = 240;
static const float GYRO_GAIN = 93870.0f;
static const int DEFAULT_AIM_SENSITIVITY = 20;
static const int MIN_AIM_SENSITIVITY = 1;
static const int MAX_AIM_SENSITIVITY = 20;
static const unsigned MAX_ROMS = 256;

static SDL_Window* window_handle = nullptr;
static SDL_Renderer* renderer = nullptr;
static SDL_Texture* game_texture = nullptr;
static TTF_Font* ui_font = nullptr;
static SDL_AudioDeviceID audio_device = 0;
static PadState pads[2];
static HidSixAxisSensorHandle gyro_handle;
static bool gyro_ready = false;
static bool split_joycons_ready = false;
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
static uint64_t previous_right_buttons = 0;
static HidAnalogStickState left_stick = {};
static unsigned split_retry_frames = 0;
static int aim_sensitivity = DEFAULT_AIM_SENSITIVITY;
static int app_language = 0;
static unsigned core_width = GAME_W;
static unsigned core_height = GAME_H;
static enum retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_XRGB8888;
static std::string status_line;
static std::unordered_map<std::string, SDL_Texture*> text_cache;

static const char* const LATIN_FONT_PATH = "romfs:/NotoSans-Regular.ttf";
static const char* const CJK_FONT_PATH = "romfs:/NotoSansCJK-Regular.ttc";

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

static void save_settings(void);
static const char* localized_read_error(void);
static const char* localized_core_error(void);

static void load_settings(void)
{
   FILE* f = fopen(CONFIG_PATH, "r");
   int version, value, language = 0;
   if (!f)
      return;
   int fields = fscanf(f, "%d %d %d", &version, &value, &language);
   if (fields >= 2 && (version == 2 || version == 3))
   {
      aim_sensitivity = std::max(MIN_AIM_SENSITIVITY,
            std::min(MAX_AIM_SENSITIVITY, value));
      if (version == 3 && fields == 3)
         app_language = std::max(0, std::min(8, language));
   }
   else
   {
      aim_sensitivity = DEFAULT_AIM_SENSITIVITY;
   }
   fclose(f);
   save_settings();
}

static void save_settings(void)
{
   FILE* f = fopen(CONFIG_PATH, "w");
   if (!f)
      return;
   fprintf(f, "3 %d %d\n", aim_sensitivity, app_language);
   fclose(f);
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
      status_line = localized_read_error();
      return false;
   }
   memset(&info, 0, sizeof(info));
   info.path = loaded_path.c_str();
   info.data = rom_data.data();
   info.size = rom_data.size();
   if (!retro_load_game(&info))
   {
      rom_data.clear();
      status_line = localized_core_error();
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
         case RETRO_DEVICE_ID_JOYPAD_B:      return (pad0_buttons & HidNpadButton_LeftSL) != 0;
         case RETRO_DEVICE_ID_JOYPAD_Y:      return (pad0_buttons & HidNpadButton_ZL) != 0;
         case RETRO_DEVICE_ID_JOYPAD_SELECT: return (pad0_buttons & HidNpadButton_LeftSL) != 0;
         case RETRO_DEVICE_ID_JOYPAD_START:  return (pad0_buttons & HidNpadButton_StickL) != 0;
         case RETRO_DEVICE_ID_JOYPAD_UP:     return (pad0_buttons & HidNpadButton_Up) != 0 || left_stick.y > 12000;
         case RETRO_DEVICE_ID_JOYPAD_DOWN:   return (pad0_buttons & HidNpadButton_Down) != 0 || left_stick.y < -12000;
         case RETRO_DEVICE_ID_JOYPAD_LEFT:   return (pad0_buttons & HidNpadButton_Left) != 0 || left_stick.x < -12000;
         case RETRO_DEVICE_ID_JOYPAD_RIGHT:  return (pad0_buttons & HidNpadButton_Right) != 0 || left_stick.x > 12000;
         case RETRO_DEVICE_ID_JOYPAD_A:      return (pad0_buttons & HidNpadButton_LeftSR) != 0;
         case RETRO_DEVICE_ID_JOYPAD_X:      return (pad0_buttons & HidNpadButton_L) != 0;
         case RETRO_DEVICE_ID_JOYPAD_L:      return (pad0_buttons & HidNpadButton_LeftSL) != 0;
         case RETRO_DEVICE_ID_JOYPAD_L2:     return (pad0_buttons & HidNpadButton_L) != 0;
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

struct MenuText
{
   const char* language_name;
   const char* title;
   const char* subtitle;
   const char* controls_title;
   const char* controls1;
   const char* controls2;
   const char* controls3;
   const char* controls4;
   const char* controls5;
   const char* warning_title;
   const char* warning1;
   const char* warning2;
   const char* language_label;
   const char* sensitivity_label;
   const char* roms_title;
   const char* no_roms;
   const char* copy_roms;
   const char* split_ready;
   const char* split_wait;
   const char* gyro_ready;
   const char* gyro_wait;
   const char* read_error;
   const char* core_error;
};

static const MenuText menu_text[] = {
   {"Русский", "ZAPP-con — ИГРЫ NES", "Игры NES с прицеливанием гироскопом Joy-Con", "КНОПКИ И УПРАВЛЕНИЕ",
    "ВЫБОР: крестовина вверх/вниз; SL или стик — играть",
    "В ИГРЕ: крестовина/стик — ходьба; SL=B; SR=A",
    "ZL/L — Turbo B/A; стик=START; MINUS=SELECT",
    "ПРАВЫЙ JOY-CON: гироприцел; ZR — выстрел",
    "PLUS — центр прицела; MINUS — меню игр",
    "ВАЖНО: ПОРЯДОК JOY-CON", "Сначала снимите ЛЕВЫЙ (игрок 1), затем ПРАВЫЙ (игрок 2).",
    "Запустите приложение после отсоединения обоих Joy-Con.", "ЯЗЫК  L/R", "ЧУВСТВИТЕЛЬНОСТЬ  ←/→", "СПИСОК ИГР NES",
    "ИГРЫ НЕ НАЙДЕНЫ", "Скопируйте ROM-файлы в /switch/gyroduckhunt/roms",
    "РАЗДЕЛЕНИЕ ГОТОВО", "ПОДКЛЮЧИТЕ JOY-CON ОТДЕЛЬНО", "ГИРОСКОП ГОТОВ", "ОЖИДАНИЕ ГИРОСКОПА",
    "Не удалось прочитать выбранную игру", "Nestopia не смогла открыть ROM"},
   {"English", "ZAPP-con — NES GAMES", "NES games with Joy-Con gyro aiming", "CONTROLS",
    "PICK: D-pad up/down; SL or stick click — play",
    "IN GAME: D-pad/stick move; SL=B; SR=A",
    "ZL/L — Turbo B/A; stick=START; MINUS=SELECT",
    "RIGHT JOY-CON: gyro aim; ZR — fire",
    "PLUS — recenter; MINUS — game list",
    "IMPORTANT: JOY-CON ORDER", "Detach LEFT Joy-Con first (player 1), then RIGHT (player 2).",
    "Launch the app after both Joy-Cons are detached.", "LANGUAGE  L/R", "AIM SPEED  ←/→", "NES GAME LIST",
    "NO GAMES FOUND", "Copy ROM files to /switch/gyroduckhunt/roms",
    "SPLIT CONTROLLERS READY", "DETACH JOY-CONS SEPARATELY", "GYRO READY", "WAITING FOR GYRO",
    "Could not read the selected game", "Nestopia could not load this ROM"},
   {"日本語", "ZAPP-con — NESゲーム", "Joy-Conのジャイロで照準を操作", "操作方法",
    "選択：十字キー上下、SLまたはスティック押し込みで決定",
    "ゲーム：十字キー／スティック移動、SL=B、SR=A",
    "ZL/L：連射B/A、スティック押し込み=START、MINUS=SELECT",
    "右Joy-Con：ジャイロ照準、ZRで発射",
    "PLUSで照準を中央へ、MINUSでゲーム一覧へ",
    "重要：Joy-Conを外す順番", "左Joy-Con（プレイヤー1）を先に、右Joy-Con（プレイヤー2）を後に外します。",
    "両方を外してからアプリを起動してください。", "言語  L/R", "照準速度  ←/→", "NESゲーム一覧",
    "ゲームがありません", "/switch/gyroduckhunt/roms にROMをコピーしてください",
    "分離接続OK", "Joy-Conを個別に接続してください", "ジャイロ準備OK", "ジャイロ待機中",
    "選択したゲームを読み込めません", "NestopiaがこのROMを読み込めません"},
   {"简体中文", "ZAPP-con — NES 游戏", "使用 Joy-Con 陀螺仪瞄准 NES 游戏", "按键说明",
    "选择：十字键上/下；按 SL 或摇杆开始",
    "游戏中：十字键/摇杆移动；SL=B；SR=A",
    "ZL/L：连发 B/A；摇杆按下=START；MINUS=SELECT",
    "右 Joy-Con：陀螺仪瞄准；ZR 开火",
    "PLUS 重新居中；MINUS 返回游戏列表",
    "重要：Joy-Con 拆卸顺序", "先拆左 Joy-Con（玩家 1），再拆右 Joy-Con（玩家 2）。",
    "两个 Joy-Con 都拆下后再启动应用。", "语言  L/R", "瞄准速度  ←/→", "NES 游戏列表",
    "未找到游戏", "将 ROM 复制到 /switch/gyroduckhunt/roms",
    "分体手柄已就绪", "请分别连接 Joy-Con", "陀螺仪已就绪", "正在等待陀螺仪",
    "无法读取所选游戏", "Nestopia 无法加载此 ROM"},
   {"Français", "ZAPP-con — JEUX NES", "Jeux NES visés au gyroscope Joy-Con", "COMMANDES",
    "Choisir : croix haut/bas ; SL ou stick pour jouer",
    "Jeu : croix/stick déplacer ; SL=B ; SR=A",
    "ZL/L : Turbo B/A ; stick=START ; MINUS=SELECT",
    "JOY-CON DROIT : visée gyro ; ZR — tirer",
    "PLUS recentre ; MINUS revient à la liste",
    "IMPORTANT : ORDRE DES JOY-CON", "Détachez d’abord le Joy-Con GAUCHE (joueur 1), puis le DROIT (joueur 2).",
    "Lancez l’application après avoir détaché les deux.", "LANGUE  L/R", "VITESSE  ←/→", "JEUX NES",
    "AUCUN JEU", "Copiez les ROM dans /switch/gyroduckhunt/roms",
    "MANETTES SÉPARÉES OK", "DÉTACHEZ LES JOY-CON", "GYRO PRÊT", "GYRO EN ATTENTE",
    "Impossible de lire le jeu", "Nestopia ne peut pas charger cette ROM"},
   {"Español", "ZAPP-con — JUEGOS NES", "Apunta con el giroscopio del Joy-Con", "CONTROLES",
    "Elegir: cruceta arriba/abajo; SL o stick para jugar",
    "Juego: cruceta/stick mover; SL=B; SR=A",
    "ZL/L: Turbo B/A; stick=START; MINUS=SELECT",
    "JOY-CON DERECHO: mira giroscópica; ZR dispara",
    "PLUS centra la mira; MINUS vuelve a la lista",
    "IMPORTANTE: ORDEN DE JOY-CON", "Desconecta primero el Joy-Con IZQUIERDO (jugador 1) y luego el DERECHO (jugador 2).",
    "Inicia la aplicación después de desconectar ambos.", "IDIOMA  L/R", "VELOCIDAD  ←/→", "JUEGOS NES",
    "NO HAY JUEGOS", "Copia ROM en /switch/gyroduckhunt/roms",
    "MANDOS SEPARADOS OK", "DESCONECTA LOS JOY-CON", "GIROSCOPIO LISTO", "ESPERANDO GIROSCOPIO",
    "No se puede leer el juego", "Nestopia no pudo cargar esta ROM"},
   {"Português", "ZAPP-con — JOGOS NES", "Mira com o giroscópio do Joy-Con", "CONTROLES",
    "Escolher: direcional cima/baixo; SL ou stick para jogar",
    "Jogo: direcional/stick mover; SL=B; SR=A",
    "ZL/L: Turbo B/A; stick=START; MINUS=SELECT",
    "JOY-CON DIREITO: mira giroscópica; ZR atira",
    "PLUS centraliza; MINUS volta à lista",
    "IMPORTANTE: ORDEM DOS JOY-CON", "Remova primeiro o Joy-Con ESQUERDO (jogador 1), depois o DIREITO (jogador 2).",
    "Abra o app depois de remover os dois Joy-Con.", "IDIOMA  L/R", "VELOCIDADE  ←/→", "JOGOS NES",
    "NENHUM JOGO", "Copie ROM para /switch/gyroduckhunt/roms",
    "CONTROLES SEPARADOS OK", "REMOVA OS JOY-CON", "GIROSCÓPIO PRONTO", "AGUARDANDO GIROSCÓPIO",
    "Não foi possível ler o jogo", "Nestopia não conseguiu carregar esta ROM"},
   {"Deutsch", "ZAPP-con — NES-SPIELE", "Zielen mit dem Joy-Con-Gyroskop", "STEUERUNG",
    "Wählen: Steuerkreuz hoch/runter; SL oder Stick startet",
    "Spiel: Steuerkreuz/Stick bewegen; SL=B; SR=A",
    "ZL/L: Turbo B/A; Stick=START; MINUS=SELECT",
    "RECHTER JOY-CON: Gyro-Ziel; ZR feuert",
    "PLUS zentriert; MINUS zurück zur Spieleliste",
    "WICHTIG: JOY-CON-REIHENFOLGE", "Zuerst den LINKEN Joy-Con (Spieler 1), danach den RECHTEN (Spieler 2) abnehmen.",
    "App erst starten, wenn beide Joy-Con abgenommen sind.", "SPRACHE  L/R", "TEMPO  ←/→", "NES-SPIELE",
    "KEINE SPIELE", "ROM-Dateien nach /switch/gyroduckhunt/roms kopieren",
    "GETRENNTE CONTROLLER OK", "JOY-CON EINZELN TRENNEN", "GYRO BEREIT", "WARTE AUF GYRO",
    "Spiel kann nicht gelesen werden", "Nestopia konnte diese ROM nicht laden"},
   {"Italiano", "ZAPP-con — GIOCHI NES", "Mira con il giroscopio Joy-Con", "COMANDI",
    "Scegli: croce su/giù; SL o stick per avviare",
    "Gioco: croce/stick muove; SL=B; SR=A",
    "ZL/L: Turbo B/A; stick=START; MINUS=SELECT",
    "JOY-CON DESTRO: mira gyro; ZR spara",
    "PLUS centra; MINUS torna alla lista giochi",
    "IMPORTANTE: ORDINE JOY-CON", "Rimuovi prima il Joy-Con SINISTRO (giocatore 1), poi il DESTRO (giocatore 2).",
    "Avvia l’app dopo aver rimosso entrambi.", "LINGUA  L/R", "VELOCITÀ  ←/→", "GIOCHI NES",
    "NESSUN GIOCO", "Copia le ROM in /switch/gyroduckhunt/roms",
    "CONTROLLER SEPARATI OK", "COLLEGA I JOY-CON SEPARATI", "GYRO PRONTO", "ATTESA GYRO",
    "Impossibile leggere il gioco", "Nestopia non riesce a caricare questa ROM"}
};

static const char* localized_read_error(void)
{
   return menu_text[std::max(0, std::min(8, app_language))].read_error;
}

static const char* localized_core_error(void)
{
   return menu_text[std::max(0, std::min(8, app_language))].core_error;
}

static void clear_text_cache(void)
{
   for (auto& item : text_cache)
      SDL_DestroyTexture(item.second);
   text_cache.clear();
}

static void close_ui_font(void)
{
   clear_text_cache();
   if (ui_font)
      TTF_CloseFont(ui_font);
   ui_font = nullptr;
}

static bool open_ui_font(void)
{
   close_ui_font();
   if (app_language == 2 || app_language == 3)
   {
      int index = app_language == 2 ? 0 : 2;
      ui_font = TTF_OpenFontIndex(CJK_FONT_PATH, 24, index);
   }
   else
      ui_font = TTF_OpenFont(LATIN_FONT_PATH, 24);
   return ui_font != nullptr;
}

static void draw_text(int x, int y, const std::string& text, int size, SDL_Color color,
      int max_width = 1180)
{
   if (!ui_font || text.empty())
      return;
   std::string key = text;
   key.push_back((char)size);
   key.push_back((char)color.r);
   key.push_back((char)color.g);
   key.push_back((char)color.b);
   key.append((const char*)&max_width, sizeof(max_width));
   auto found = text_cache.find(key);
   if (found == text_cache.end())
   {
      TTF_SetFontSize(ui_font, size);
      SDL_Surface* surface = TTF_RenderUTF8_Blended_Wrapped(
            ui_font, text.c_str(), color, (Uint32)max_width);
      if (!surface)
         return;
      SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
      SDL_FreeSurface(surface);
      if (!texture)
         return;
      found = text_cache.emplace(key, texture).first;
   }
   int width = 0, height = 0;
   SDL_QueryTexture(found->second, nullptr, nullptr, &width, &height);
   SDL_Rect dst = { x, y, width, height };
   SDL_RenderCopy(renderer, found->second, nullptr, &dst);
}

static std::string short_name(std::string name)
{
   size_t dot = name.find_last_of('.');
   if (dot != std::string::npos)
      name.resize(dot);
   if (name.size() > 28)
   {
      size_t end = 25;
      while (end > 0 && (((unsigned char)name[end] & 0xc0) == 0x80))
         end--;
      name = name.substr(0, end) + "...";
   }
   return name;
}

static void render_menu(void)
{
   const MenuText& t = menu_text[app_language];
   SDL_SetRenderDrawColor(renderer, 6, 10, 18, 255);
   SDL_RenderClear(renderer);
   SDL_Color white = { 240, 245, 255, 255 };
   SDL_Color cyan = { 80, 220, 255, 255 };
   SDL_Color muted = { 171, 190, 215, 255 };
   SDL_Color panel = { 16, 27, 43, 255 };
   SDL_Color border = { 40, 78, 107, 255 };
   SDL_Rect card = { 38, 28, 1204, 664 };
   SDL_SetRenderDrawColor(renderer, panel.r, panel.g, panel.b, panel.a);
   SDL_RenderFillRect(renderer, &card);
   SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
   SDL_RenderDrawRect(renderer, &card);
   SDL_Rect header = { 39, 29, 1202, 104 };
   SDL_SetRenderDrawColor(renderer, 11, 39, 61, 255);
   SDL_RenderFillRect(renderer, &header);
   draw_text(64, 47, t.title, 32, cyan);
   draw_text(66, 91, t.subtitle, 18, muted);

   /* Retro target and zapper silhouette as lightweight SDL artwork. */
   SDL_SetRenderDrawColor(renderer, 255, 88, 90, 255);
   for (int r = 12; r <= 42; r += 15)
   {
      SDL_Rect ring = { 1118 - r, 80 - r, r * 2, r * 2 };
      SDL_RenderDrawRect(renderer, &ring);
   }
   SDL_RenderDrawLine(renderer, 1066, 80, 1170, 80);
   SDL_RenderDrawLine(renderer, 1118, 28, 1118, 132);
   SDL_SetRenderDrawColor(renderer, 83, 172, 202, 255);
   SDL_Rect gun_barrel = { 1040, 107, 78, 18 };
   SDL_Rect gun_grip = { 1082, 123, 23, 41 };
   SDL_RenderFillRect(renderer, &gun_barrel);
   SDL_RenderFillRect(renderer, &gun_grip);
   SDL_SetRenderDrawColor(renderer, 13, 48, 69, 255);
   SDL_Rect gun_highlight = { 1046, 112, 63, 6 };
   SDL_RenderFillRect(renderer, &gun_highlight);

   SDL_Rect controls = { 54, 151, 712, 193 };
   SDL_Rect warning = { 784, 151, 442, 193 };
   SDL_SetRenderDrawColor(renderer, 11, 19, 32, 255);
   SDL_RenderFillRect(renderer, &controls);
   SDL_RenderFillRect(renderer, &warning);
   SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
   SDL_RenderDrawRect(renderer, &controls);
   SDL_RenderDrawRect(renderer, &warning);
   draw_text(72, 164, t.controls_title, 21, cyan);
   draw_text(72, 195, t.controls1, 17, white, 675);
   draw_text(72, 222, t.controls2, 17, white, 675);
   draw_text(72, 249, t.controls3, 17, white, 675);
   draw_text(72, 276, t.controls4, 17, white, 675);
   draw_text(72, 312, t.controls5, 17, muted);
   draw_text(802, 164, t.warning_title, 19, SDL_Color{255, 190, 94, 255});
   draw_text(802, 202, t.warning1, 15, white, 402);
   draw_text(802, 244, t.warning2, 15, white, 402);
   draw_text(802, 285, t.language_label, 17, cyan);
   draw_text(1004, 285, menu_text[app_language].language_name, 19, white);

   SDL_Rect settings = { 54, 357, 1172, 59 };
   SDL_SetRenderDrawColor(renderer, 11, 19, 32, 255);
   SDL_RenderFillRect(renderer, &settings);
   SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
   SDL_RenderDrawRect(renderer, &settings);
   char sensitivity[96];
   snprintf(sensitivity, sizeof(sensitivity), "%s: %dX", t.sensitivity_label, aim_sensitivity);
   draw_text(72, 374, sensitivity, 19, cyan);
   draw_text(602, 376, t.roms_title, 19, white);
   draw_text(980, 374, split_joycons_ready ? t.split_ready : t.split_wait, 14,
         split_joycons_ready ? cyan : SDL_Color{255, 180, 80, 255});
   draw_text(980, 396, gyro_ready ? t.gyro_ready : t.gyro_wait, 14,
         gyro_ready ? cyan : SDL_Color{255, 100, 80, 255});
   if (roms.empty())
   {
      draw_text(76, 468, t.no_roms, 27, white);
      draw_text(76, 518, t.copy_roms, 18, cyan);
   }
   else
   {
      int first = std::max(0, std::min(selected_rom - 5, (int)roms.size() - 10));
      int last = std::min((int)roms.size(), first + 10);
      for (int i = first; i < last; i++)
      {
         int y = 432 + (i - first) * 23;
         if (i == selected_rom)
         {
            SDL_Rect highlight = { 64, y - 2, 1138, 22 };
            SDL_SetRenderDrawColor(renderer, 36, 76, 120, 255);
            SDL_RenderFillRect(renderer, &highlight);
         }
         draw_text(78, y, short_name(roms[i]), 17, i == selected_rom ? cyan : white);
      }
   }
   if (!status_line.empty())
      draw_text(66, 664, status_line, 16, SDL_Color{255, 100, 80, 255});
   SDL_RenderPresent(renderer);
}

static void poll_switch_input(void)
{
   padUpdate(&pads[0]);
   padUpdate(&pads[1]);
   pad0_buttons = padGetButtons(&pads[0]);
      left_stick = padGetStickPos(&pads[0], 0);
   right_buttons = (uint64_t)padGetButtons(&pads[1]);

   bool recenter_combo =
      (right_buttons & (HidNpadButton_Plus | HidNpadButton_StickR)) ==
      (HidNpadButton_Plus | HidNpadButton_StickR);
   bool recenter_pressed =
      ((right_buttons & HidNpadButton_Plus) && !(previous_right_buttons & HidNpadButton_Plus)) ||
      (recenter_combo &&
       (previous_right_buttons & (HidNpadButton_Plus | HidNpadButton_StickR)) !=
       (HidNpadButton_Plus | HidNpadButton_StickR));
   previous_right_buttons = right_buttons;
   if (recenter_pressed)
   {
      cursor_x = cursor_y = 32767;
      cursor_dx = cursor_dy = 0;
   }
   else if (gyro_ready)
   {
      HidSixAxisSensorState state;
      if (hidGetSixAxisSensorStates(gyro_handle, &state, 1) > 0 && state.delta_time)
      {
         float dt = std::min(0.05f, (float)state.delta_time / 1000000000.0f);
         float scale = (float)aim_sensitivity;
         float dx = -state.angular_velocity.z * dt * GYRO_GAIN * scale;
         float dy = -state.angular_velocity.x * dt * GYRO_GAIN * scale;
         int ix = (int)dx;
         int iy = (int)dy;
         cursor_x = std::max(0, std::min(65534, cursor_x + ix));
         cursor_y = std::max(0, std::min(65534, cursor_y + iy));
         cursor_dx += ix;
         cursor_dy += iy;
      }
   }
}

static void initialize_right_gyro(void)
{
   if (gyro_ready)
      return;
   if (R_SUCCEEDED(hidGetSixAxisSensorHandles(&gyro_handle, 1,
               HidNpadIdType_No2, HidNpadStyleTag_NpadJoyRight)) &&
         R_SUCCEEDED(hidStartSixAxisSensor(gyro_handle)))
      gyro_ready = true;
}

static void assign_split_joycons(void)
{
   Result left_result = hidSetNpadJoyAssignmentModeSingle(
         HidNpadIdType_No1, HidNpadJoyDeviceType_Left);
   Result right_result = hidSetNpadJoyAssignmentModeSingle(
         HidNpadIdType_No2, HidNpadJoyDeviceType_Right);
   split_joycons_ready = R_SUCCEEDED(left_result) && R_SUCCEEDED(right_result);
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
   if (R_FAILED(romfsInit()))
      return 6;
   if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
      return 1;
   SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
   window_handle = SDL_CreateWindow("ZAPP-con", SDL_WINDOWPOS_CENTERED,
         SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H, SDL_WINDOW_FULLSCREEN);
   if (!window_handle)
      return 2;
   renderer = SDL_CreateRenderer(window_handle, -1,
         SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
   if (!renderer)
      renderer = SDL_CreateRenderer(window_handle, -1, SDL_RENDERER_SOFTWARE);
   if (!renderer)
      return 3;
   if (TTF_Init() != 0)
      return 4;
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
   /* Force detached Joy-Cons into separate player slots with fixed handedness. */
   assign_split_joycons();
   padInitialize(&pads[0], HidNpadIdType_No1);
   padInitialize(&pads[1], HidNpadIdType_No2);
   padUpdate(&pads[0]);
   padUpdate(&pads[1]);
   initialize_right_gyro();

   start_core();
   scan_roms();
   load_settings();
   if (!open_ui_font())
      return 5;
   frontend_ready = true;
   bool running = true;
   uint64_t previous_buttons = 0;
   while (running && appletMainLoop())
   {
      poll_switch_input();
      if (!split_joycons_ready && ++split_retry_frames >= 60)
      {
         split_retry_frames = 0;
         assign_split_joycons();
      }
      initialize_right_gyro();
      uint64_t buttons = padGetButtons(&pads[0]);
      uint64_t pressed = buttons & ~previous_buttons;
      previous_buttons = buttons;

      if (!game_loaded)
      {
         int old_language = app_language;
         if (pressed & HidNpadButton_L)
            app_language = (app_language + 8) % 9;
         if (pressed & HidNpadButton_R)
            app_language = (app_language + 1) % 9;
         if (app_language != old_language)
         {
            save_settings();
            if (!open_ui_font())
               app_language = old_language;
         }
         if (!roms.empty() && (pressed & HidNpadButton_Up))
            selected_rom = std::max(0, selected_rom - 1);
         if (!roms.empty() && (pressed & HidNpadButton_Down))
            selected_rom = std::min((int)roms.size() - 1, selected_rom + 1);
         if (pressed & HidNpadButton_Left)
         {
            aim_sensitivity = std::max(MIN_AIM_SENSITIVITY, aim_sensitivity - 1);
            save_settings();
         }
         if (pressed & HidNpadButton_Right)
         {
            aim_sensitivity = std::min(MAX_AIM_SENSITIVITY, aim_sensitivity + 1);
            save_settings();
         }
         if ((pressed & (HidNpadButton_LeftSL | HidNpadButton_StickL)) && !roms.empty())
            load_selected_rom();
         if (pressed & HidNpadButton_Minus)
            scan_roms();
         if ((pressed & HidNpadButton_Plus) || (right_buttons & HidNpadButton_Plus))
            running = false;
         if (roms.empty() || !game_loaded)
            render_menu();
      }
      else
      {
         if (buttons & HidNpadButton_Minus)
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
   close_ui_font();
   if (game_texture)
      SDL_DestroyTexture(game_texture);
   if (renderer)
      SDL_DestroyRenderer(renderer);
   if (window_handle)
      SDL_DestroyWindow(window_handle);
   TTF_Quit();
   romfsExit();
   SDL_Quit();
   return 0;
}
