#!/bin/sh
# Cross-compile SplashLoader.asi with MinGW-w64 (i686). Visual Studio users: open SplashLoader.vcxproj (Release|Win32).
set -e
mkdir -p build/compat
printf '#include <windows.h>\n#include <cstdint>\n#include <vector>\n' > build/compat/Windows.h   # MinGW header-case shim
i686-w64-mingw32-g++ -std=c++20 -O2 -shared -static -static-libgcc -static-libstdc++ -s \
  -Wno-narrowing -Wno-overflow -Wno-multichar \
  -D_CRT_SECURE_NO_WARNINGS -D_CRT_NON_CONFORMING_SWPRINTFS \
  -Ibuild/compat -IFIFAAPI -I. Main.cpp -o build/SplashLoader.asi
echo built build/SplashLoader.asi
