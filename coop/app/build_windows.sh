#!/bin/bash
# Cross-builds bbcoop.exe for Windows from Ubuntu/WSL. Run from coop/app:  ./build_windows.sh
# Produces windows/bbcoop.exe and windows/SDL2.dll; give both to each player with the ROM.
set -e
MGBA_COMMIT=662dada8b79070bde93829a44d583c517fc5ec2d
SDL_VERSION=2.30.9
cd "$(dirname "$0")"

sudo apt-get install -y mingw-w64 cmake ninja-build git curl

# 1. SDL2 for MinGW
if [ ! -d SDL2-$SDL_VERSION ]; then
    curl -L -o sdl2-mingw.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-devel-$SDL_VERSION-mingw.tar.gz
    tar xzf sdl2-mingw.tar.gz
fi
SDL=$PWD/SDL2-$SDL_VERSION/x86_64-w64-mingw32

# 2. mGBA's core library for Windows
if [ ! -d mgba ]; then
    git clone https://github.com/mgba-emu/mgba.git
fi
git -C mgba fetch --quiet origin && git -C mgba checkout --quiet $MGBA_COMMIT
rm -rf mgba/build-win
cmake -S mgba -B mgba/build-win -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
    -DBUILD_STATIC=ON -DBUILD_SHARED=OFF -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_HEADLESS=OFF \
    -DM_CORE_GB=OFF -DM_CORE_GBA=ON -DENABLE_SCRIPTING=OFF -DENABLE_DEBUGGERS=OFF -DENABLE_GDB_STUB=OFF \
    -DUSE_PNG=OFF -DUSE_ZLIB=OFF -DUSE_LZMA=OFF -DUSE_MINIZIP=OFF -DUSE_LIBZIP=OFF -DUSE_FREETYPE=OFF \
    -DUSE_SQLITE3=OFF -DUSE_ELF=OFF -DUSE_EDITLINE=OFF -DUSE_JSON_C=OFF -DUSE_FFMPEG=OFF -DUSE_LUA=OFF \
    -DBUILD_GL=OFF -DBUILD_GLES2=OFF -DBUILD_GLES3=OFF -DUSE_EPOXY=OFF -DUSE_DISCORD_RPC=OFF
ninja -C mgba/build-win mgba

# 3. the app
DEFS=$(grep -m1 -A3 "CMakeFiles/mgba.dir/src/core/core.c.o:" mgba/build-win/build.ninja | grep DEFINES | sed 's/ *DEFINES = //')
mkdir -p windows
x86_64-w64-mingw32-windres bbcoop.rc -O coff -o windows/bbcoop_res.o
x86_64-w64-mingw32-gcc -O2 -std=gnu11 -D_WIN32_WINNT=0x0601 $DEFS -Imgba/include -Imgba/build-win/include -Imgba/src \
    -I$SDL/include/SDL2 bbcoop.c windows/bbcoop_res.o -o windows/bbcoop.exe mgba/build-win/libmgba.a \
    -L$SDL/lib -lSDL2 -lws2_32 -lshlwapi -liphlpapi -lcomdlg32 -lcomctl32 -lgdi32 -luser32 -lm -static-libgcc -mwindows
rm -f windows/bbcoop_res.o
cp $SDL/bin/SDL2.dll windows/
echo "Built windows/bbcoop.exe (keep SDL2.dll next to it). Double-click it to open the launcher."
