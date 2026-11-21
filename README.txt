SplashLoader.asi v1.0 - custom boot splash for FIFA 11 (PC)
=============================================================

INSTALL
  1. Copy SplashLoader.asi and SplashLoader.ini into   FIFA 11\Game\plugins\
  2. Put your image in   FIFA 11\Game\data\fifarna\   as one of:
        splash.fsh   splash.dds   splash.png   splash.jpg (or .jpeg)
  3. Start the game.
  No splash file = the original FIFA 11 splash.

WHICH FILE IS USED  (plugins\SplashLoader.ini, [Splash] section)
  Enabled=1      0 = plugin does nothing, original splash
  Format=auto    auto: first one found in this order:  fsh > dds > png > jpg
                 or force one format: fsh / dds / png / jpg
  File=          optional specific file (relative to the Game folder, or a full path);
                 overrides Format - e.g. File=data\fifarna\splash_winter.png
  Without the ini file the plugin behaves as Enabled=1, Format=auto.
  A forced Format / File that does not exist shows a warning and uses the original splash.
  The file type is detected from its contents, so a mis-named file still loads.

IMAGE
  * Best: 1920x1080 or 1280x720 PNG, or an uncompressed DDS. The splash is drawn as a 16:9 quad,
    so 16:9 images fit with no stretching.
  * FIFA 11's boot texture bank can only display DXT1 (it knows FSH 0x60/0x61/0x62 only, and its
    copy loop is correct for DXT1 only). So every input is converted to DXT1 by the plugin with a
    high-quality encoder (rgbcx, level 14). DXT1 input (DDS or FSH) is used as-is.
    The converted result is cached in plugins\SplashLoader.cache and only redone when the
    source file changes (that first launch takes ~0.5-1 s longer).
  * Accepted:
      PNG (8/16-bit, with or without alpha), JPG (baseline/progressive)
      DDS: A8R8G8B8, X8R8G8B8, A8B8G8R8, R8G8B8, DXT1/3/5, DX10-header equivalents
      FSH: DXT1/3/5, 8888, 888, 565, 1555, 4444, 8-bit palettized; uncompressed, RefPack (QFS)
           or chunkzip-wrapped (like the one inside fifa.exe). The first image entry is used.
  * Transparency is not shown: transparent pixels are blended over black.
  * Max 4096x4096. DXT inputs need width/height divisible by 4.
  * A broken or unsupported file gives a warning and the original splash.

LOG
  plugins\SplashLoader.log: game version, config, which file was picked (and which were ignored),
  what was read, whether the cache was used, and the patched addresses. Overwritten every start.

HOW IT WORKS
  fifa.exe keeps splash.fsh as a "chunkzip" blob in its .data section
  (v1.0.1.0 FLT: blob at 0x12F1768, size dword at 0x12F1764) and registers it as an in-memory
  file "splash.fsh" (loader at 0x8FA9D0). The plugin builds a DXT1 FSH in memory (same layout as
  the original: dir "G359", entry "fifa", "fifasplash" attachment) and redirects the pointer
  operand (0x8FAA57) and the size operand (0x8FAA36) to it. The resource system only
  decompresses chunkzip/chunkref/chunklzx, so a raw SHPI loads directly.
  Verified game versions (the scanner finds exactly one site in each):
    FIFA 11 v1.0.1.0 FLT (entry 0240E9AF): string 011C9344, ptr operand 008FAA57, size operand 008FAA36
    FIFA 11 v1.0.0.0 RLD (entry 00461846): string 011C82C4, ptr operand 008F9617, size operand 008F95F6
  Both carry the identical splash blob and the same DXT1-only boot texture loader.
  The addresses are found by scanning (string "splash.fsh" -> code reference -> nearby operand
  pointing at a chunkzip/SHPI blob + operand pointing at the size dword before it), so other
  fifa.exe builds with the same loader code should work without changes.

BUILD
  Visual Studio: open SplashLoader.vcxproj, Release | Win32.
  Linux/MinGW:   ./build.sh
  Tests:         cd test && g++ -std=c++20 -O2 test_core.cpp -o test_core && g++ -std=c++20 test_config.cpp -o test_config
                 ./test_core fifa.exe orig_splash.dds orig_splash.fsh [more files ...] ; ./test_config

CREDITS
  DXT1 encoder: rgbcx by Richard Geldreich (public domain / MIT) - vendor/rgbcx_LICENSE.txt
  PNG/JPG decoder: stb_image by Sean Barrett (public domain / MIT) - vendor/stb_image.h
