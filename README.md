# SplashLoader

**Custom boot splash screens for FIFA 11 (PC)** — no TexMod, no exe editing.

FIFA 11 is the odd one out: unlike FIFA 12 and later, it has no separate `splash.fsh` on disk. The splash is compressed and built into `fifa.exe` itself, so for 16 years the only way to change it was TexMod or a patched executable.

SplashLoader is a plugin for the [FIFA 11 Plugin Loader](https://soccergaming.com/forums/threads/fifa-11-plugin-loader.6475247/). Drop an image into the game folder and it shows up as the splash, the same way it works in the newer games, and it can be shipped with any mod.

![SplashLoader demo](splashloader_demo.png)

## Features

- Use **PNG, JPG, DDS or FSH** as the splash — any of them just works
- Automatic conversion to the format the game needs, using a high-quality DXT1 encoder
- Converted result is cached, so there's no extra load time after the first launch
- Optional `SplashLoader.ini` to pick the format, point to a specific file, or turn the plugin off
- No hardcoded addresses: the plugin finds the splash loader in `fifa.exe` by scanning, so it works across game builds
- A broken or unsupported image never crashes the game — you get a warning and the original splash

## Requirements

- FIFA 11 (PC)
- [FIFA 11 Plugin Loader](https://soccergaming.com/forums/threads/fifa-11-plugin-loader.6475247/)

## Installation

1. Install the FIFA 11 Plugin Loader.
2. Copy `SplashLoader.asi` and `SplashLoader.ini` into `FIFA 11\Game\plugins\`.
3. Put your splash image in `FIFA 11\Game\data\fifarna\`, named one of:
   `splash.png` · `splash.jpg` / `splash.jpeg` · `splash.dds` · `splash.fsh`
4. Start the game.

No splash file = the original FIFA 11 splash.

## Making a splash

- **Best size: 1920×1080 or 1280×720.** The game draws the splash as a 16:9 quad, so 16:9 images fit with no stretching. (The original is 1024×512 and gets stretched.)
- **Best source: PNG or uncompressed DDS.** The plugin's encoder usually beats exporting DXT1 yourself.
- Transparency isn't shown: transparent pixels are blended over black.
- Maximum size is 4096×4096. DXT-compressed inputs need width and height divisible by 4.

Supported inputs:

| Type | Formats |
|------|---------|
| PNG | 8/16-bit, with or without alpha |
| JPG | baseline and progressive |
| DDS | A8R8G8B8, X8R8G8B8, A8B8G8R8, R8G8B8, DXT1/3/5, DX10-header equivalents |
| FSH | DXT1/3/5, 8888, 888, 565, 1555, 4444, 8-bit palettized — uncompressed, RefPack (QFS) or chunkzip; the first image entry is used |

The file type is detected from its contents, so a JPG accidentally named `.png` still loads.

## Configuration

`plugins\SplashLoader.ini`:

```ini
[Splash]
; 1 = custom splash on, 0 = original FIFA 11 splash
Enabled=1

; auto = first one found, in this order: fsh > dds > png > jpg
; or force one format: fsh / dds / png / jpg
Format=auto

; Optional: a specific file (relative to the Game folder, or a full path). Overrides Format.
; File=data\fifarna\splash_winter.png
File=
```

- Without the ini file, the plugin behaves as `Enabled=1`, `Format=auto`.
- If a forced `Format` or `File` doesn't exist, you get a warning and the original splash — it never silently falls back to a different file.

## Troubleshooting

`plugins\SplashLoader.log` is rewritten on every launch. It shows the detected game version, the config, which file was picked (and which were ignored), what was read, whether the cache was used, and the patched addresses. Include it with any bug report.

To force a re-encode, delete `plugins\SplashLoader.cache`. It's also rebuilt automatically whenever the source image changes.

## Compatibility

| Game version | Entry point | Status |
|---|---|---|
| FIFA 11 v1.0.1.0 FLT | `0240E9AF` | Tested in game |
| FIFA 11 v1.0.0.0 RELOADED | `00461846` | Verified offline (scanner + loader code) |

Other builds with the same loader code should work too. If the plugin can't find the splash loader, it shows a warning and the game uses the original splash.

## How it works

For anyone who wants to dig further:

1. **Where the splash lives.** `fifa.exe` keeps `splash.fsh` as an EA `chunkzip` blob (two deflate chunks) in its `.data` section, with the blob's size stored in the dword right before it. In v1.0.1.0 FLT the blob is at `0x12F1768` and the size at `0x12F1764`. Decompressed, it's an ordinary FSH: directory id `G359`, one entry `fifa`, DXT1 1024×512 with no mipmaps, plus a `fifasplash` name attachment.
2. **How it's loaded.** The FifaRNA `SplashScreen` loader (`0x8FA9D0` in FLT) registers the blob with the resource system as an in-memory file — name `"splash.fsh"`, pointer, size — through a 20-byte parameter struct `{0xFF, 0, ptr, size, flags}`. The resource system only decompresses data that starts with `chunkref` / `chunkzip` / `chunklzx`, so a raw `SHPI` is accepted as-is.
3. **The DXT1-only limitation.** The boot texture bank's FSH format mapper (`0xC89D40` in FLT) only knows codes `0x60`/`0x61`/`0x62` (DXT1/3/5). Anything else is skipped and the splash thread waits forever. Its copy loop (`0xC8A5E1`) also copies `2 × width` bytes per block row, which is only correct for DXT1. That's why every input is converted to DXT1.
4. **The patch.** The plugin builds a DXT1 FSH in memory with exactly the original layout, then redirects two operands: the blob pointer (`0x8FAA57` in FLT) and the address the size is read from (`0x8FAA36`).
5. **Finding the patch sites.** Instead of hardcoding addresses, the plugin looks for the string `"splash.fsh"`, then the code that references it. Within a small window around that reference, it finds the operand pointing at a `chunkzip`/`SHPI` blob and the operand pointing at the size dword just before it.

   | | FLT 1.0.1.0 | RELOADED 1.0.0.0 |
   |---|---|---|
   | `"splash.fsh"` string | `011C9344` | `011C82C4` |
   | pointer operand | `008FAA57` | `008F9617` |
   | size operand | `008FAA36` | `008F95F6` |

6. **Rendering.** `SplashScreen::Render` reads the backbuffer size, scales by 0.5625 (9/16) and draws a letterboxed 16:9 quad with UVs 0→1 — hence 16:9 images fit best.

## Building

**Visual Studio:** open `SplashLoader.vcxproj` and build `Release | Win32`.

**MinGW-w64 (Linux/WSL):**

```sh
./build.sh
```

This produces `build/SplashLoader.asi`, a 32-bit DLL depending only on `KERNEL32`, `msvcrt` and `USER32`.

### Tests

The core logic has no Windows dependencies and is tested natively:

```sh
cd test
g++ -std=c++20 -O2 test_core.cpp -o test_core
g++ -std=c++20 -O2 test_config.cpp -o test_config

./test_core path/to/fifa.exe orig_splash.dds orig_splash.fsh [more images...]
./test_config
```

- `test_core` maps `fifa.exe` like the Windows loader, runs the scanner, checks that rebuilding the original splash is byte-identical, and decodes and converts any extra images you pass it.
- `test_config` covers the ini and priority rules.

Game files are not included in this repo. Use your own `fifa.exe`.

## Project layout

```
Main.cpp          plugin entry: config, file selection, cache, patching
SplashCore.h      DDS parsing, DXT1 encoding, FSH building, patch-site scanner
SplashInputs.h    FSH (raw/RefPack/chunkzip), PNG and JPG decoding
SplashConfig.h    ini rules and format priority
SplashLoader.ini  default settings
FIFAAPI/          FIFA plugin SDK by Dmitri
vendor/           rgbcx (DXT1 encoder), stb_image (PNG/JPG decoder)
test/             native tests
```

## Credits

- **Dmitri** — [FIFA 11 Plugin Loader](https://soccergaming.com/forums/threads/fifa-11-plugin-loader.6475247/) and the FIFA plugin SDK (`FIFAAPI/`)
- **Richard Geldreich** — [rgbcx](https://github.com/richgel999/bc7enc) DXT1 encoder (public domain / MIT, see `vendor/rgbcx_LICENSE.txt`)
- **Sean Barrett** — [stb_image](https://github.com/nothings/stb) (public domain / MIT)

## License

MIT — see [LICENSE](LICENSE). Third-party code in `FIFAAPI/` and `vendor/` keeps its own license.

FIFA is a trademark of Electronic Arts. This project is not affiliated with or endorsed by EA.
