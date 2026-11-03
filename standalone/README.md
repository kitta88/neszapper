# Gyro Zapper NES for Nintendo Switch

A standalone Switch homebrew frontend for the Nestopia libretro core. It starts with a ROM picker and runs NES/Famicom content from the microSD card. At startup it requests separate controller slots for a left Joy-Con on player 1 and a right Joy-Con on player 2. Zapper games use the right Joy-Con gyro as an on-screen pointer and ZR as the trigger.

## Install and use

1. Copy `gyroduckhunt.nro` to `sd:/switch/gyroduckhunt/`.
2. Put uncompressed `.nes`, `.unf`, `.unif`, `.fds`, or `.nsf` files in `sd:/switch/gyroduckhunt/roms/`.
3. Launch `gyroduckhunt.nro` from the Homebrew Menu.
4. Select a game with the left Joy-Con D-pad and press SL or stick click. While playing, use the right Joy-Con as the pointer and hold ZR to fire.
5. Point the right Joy-Con at the screen center and press Plus + right stick click to recenter. Press Minus + SL to return to the ROM list.
6. In the ROM picker, use the left Joy-Con D-pad Left/Right to change gyro sensitivity from 1Г— to 20Г—. The default is 8Г— and the setting is saved in `config.ini`.

The app creates `saves/` beside the ROM directory. It does not include ROM files; only use games you are entitled to use.

## Default controls

| Input | Action |
| --- | --- |
| Left Joy-Con D-pad / stick | NES directions |
| Left Joy-Con SL / SR | NES B / A |
| Left Joy-Con ZL / L | Turbo B / turbo A |
| Left Joy-Con stick click / minus | Start / Select |
| Left Joy-Con minus + SL | Return to ROM list |
| Right Joy-Con gyro | Aim; up moves the cursor up |
| Right Joy-Con ZR | Zapper trigger |
| Right Joy-Con Plus + stick click | Recenter aim |

The menu displays the button map, sensitivity and status of the split Joy-Con assignments and right gyro. The camera pointer is attached to Nestopia's port 2 Zapper input. RetroArch's touchscreen overlay and controller-device selection do not take part.

## Build

The GitHub Actions workflow builds the NRO using devkitPro/libnx and the Nestopia Switch static core. Run it from the repository's Actions tab with **Build Gyro Zapper NES for Switch в†’ Run workflow**, then download the `gyroduckhunt-switch-homebrew` artifact from the completed run.

