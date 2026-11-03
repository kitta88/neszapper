# Gyro Zapper NES for Nintendo Switch

A standalone Switch homebrew frontend for the Nestopia libretro core. It starts with a ROM picker and runs NES/Famicom content from the microSD card. Zapper games use the right Joy-Con gyro as an on-screen pointer and ZR as the trigger; the left Joy-Con controls player 1.

## Install and use

1. Copy `gyroduckhunt.nro` to `sd:/switch/gyroduckhunt/`.
2. Put uncompressed `.nes`, `.unf`, `.unif`, `.fds`, or `.nsf` files in `sd:/switch/gyroduckhunt/roms/`.
3. Launch `gyroduckhunt.nro` from the Homebrew Menu.
4. Select a game with the left Joy-Con D-pad and press A. While playing, use the right Joy-Con as the pointer and hold ZR to fire.
5. Point the right Joy-Con at the screen center and press Plus + right stick click to recenter. Press Minus to return to the ROM list.

The app creates `saves/` beside the ROM directory. It does not include ROM files; only use games you are entitled to use.

## Controls

| Input | Action |
| --- | --- |
| Left Joy-Con D-pad / stick | NES directions |
| Left Joy-Con L / R | NES B / A |
| Left Joy-Con L + ZL | NES B / turbo B |
| Left Joy-Con minus / SL | Select / return to ROM list |
| Left Joy-Con stick click | Start |
| Right Joy-Con gyro | Aim |
| Right Joy-Con ZR | Zapper trigger |
| Right Joy-Con Plus + stick click | Recenter aim |

The camera pointer is attached to Nestopia's port 2 Zapper input. This app uses the standalone Nestopia core directly, so RetroArch's touchscreen overlay and controller-device selection do not take part.

## Build

The GitHub Actions workflow builds the NRO using devkitPro/libnx and the Nestopia Switch static core. Run it from the repository's **Actions** tab with **Build Gyro Zapper NES for Switch в†’ Run workflow**, then download the `gyroduckhunt-switch-homebrew` artifact from the completed run.

