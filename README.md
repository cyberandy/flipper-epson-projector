# Epson Projector Remote for Flipper Zero

An infrared remote for Epson projectors. Tested target: **Epson EB-U05 3LCD**. No Bluetooth and no line-of-sight pairing; it transmits with the Flipper's built-in IR LED, so point the top of the Flipper at the projector.

Runs on the official Flipper Zero firmware, built against **1.4.3 / API 87.1**.

Independent project, not affiliated with Epson or Flipper Devices.

## Install

Build it (see below) or download `epson_projector.fap` from the GitHub Actions artifacts, then copy it to `SD Card/apps/Infrared/` with qFlipper. Start it from **Apps > Infrared > Epson Projector**.

## Controls

Main screen, for navigating the projector's own menu:

| Flipper button | Sends |
| --- | --- |
| Arrows | Up / Down / Left / Right; hold to repeat |
| Short OK | Enter |
| Short Back | Esc |
| Hold OK | Opens the actions menu |
| Hold Back | Exit app |

Actions menu (Up/Down to choose, short OK to send, short Back to close):

| Item | Epson function |
| --- | --- |
| Power | Power on, or power off (press twice; the projector asks to confirm) |
| Source search | Cycle to the next active input |
| Menu | Open/close the on-screen menu |
| A/V Mute (blank) | Blank the picture and sound |
| Volume + / - | Speaker volume |
| Video source | Select the video input |
| Color mode | Cycle color modes |
| Aspect | Cycle aspect ratio |
| Auto adjust | Re-sync the analog image |
| Back to remote | Close the menu |

## How the codes were chosen

Epson projectors use NEC extended infrared at address `0x5583` (bytes `83 55`). Each command byte was cross-checked across three independent sources and all agree:

- The Flipper firmware's `projector.ir` library (power, volume, mute).
- The official LIRC configs `Epson_12807990` and `Epson_ELPST12` (adds Enter, Esc, arrows, source search, color mode, aspect, auto).
- Power `0x90` was additionally confirmed on real Epson hardware.

Command bytes used (address `0x5583`): Power `0x90`, Source search `0x8C`, Menu `0x9A`, A/V Mute `0x93`, Vol+ `0x98`, Vol- `0x99`, Video `0x9C`, Color mode `0x8F`, Aspect `0x8A`, Auto `0x9E`, Up `0xB0`, Down `0xB2`, Left `0xB3`, Right `0xB1`, Enter `0x85`, Esc `0x84`.

If a button does nothing on a particular model, that model likely remaps it; capture the real remote with the Flipper's Infrared > Learn and tell me the protocol, address and command.

## Build

```sh
pipx install ufbt
ufbt update --branch=1.4.3
ufbt            # builds dist/epson_projector.fap
ufbt launch     # installs and runs it on a USB-connected Flipper (close qFlipper first)
```

## Credits and license

Special thanks to **G50 SEOktoberfest**, who gifted me my first Flipper Zero.

The app structure follows [flipper-google-tv-remote](https://github.com/cyberandy/flipper-google-tv-remote), which is GPL-3.0-only, so this project is also GPL-3.0-only. See [LICENSE](LICENSE). IR command bytes come from the Flipper firmware IR library and the public LIRC remote database.
