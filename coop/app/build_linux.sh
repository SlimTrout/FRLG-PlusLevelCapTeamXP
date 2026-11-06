#!/bin/bash
# Builds the Brainy Blue Co-op app for Linux (also runs on Windows 11 through WSL).
# Run from coop/app:  ./build_linux.sh
set -e
MGBA_COMMIT=662dada8b79070bde93829a44d583c517fc5ec2d
cd "$(dirname "$0")"

sudo apt-get install -y build-essential cmake ninja-build git libsdl2-dev

# 1. mGBA's core library (static, GBA only)
if [ ! -d mgba ]; then
    git clone https://github.com/mgba-emu/mgba.git
fi
git -C mgba fetch --quiet origin && git -C mgba checkout --quiet $MGBA_COMMIT
rm -rf mgba/build-linux
cmake -S mgba -B mgba/build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_STATIC=ON -DBUILD_SHARED=OFF -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_HEADLESS=OFF \
    -DM_CORE_GB=OFF -DM_CORE_GBA=ON -DENABLE_SCRIPTING=OFF -DENABLE_DEBUGGERS=OFF -DENABLE_GDB_STUB=OFF \
    -DUSE_PNG=OFF -DUSE_ZLIB=OFF -DUSE_LZMA=OFF -DUSE_MINIZIP=OFF -DUSE_LIBZIP=OFF -DUSE_FREETYPE=OFF \
    -DUSE_SQLITE3=OFF -DUSE_ELF=OFF -DUSE_EDITLINE=OFF -DUSE_JSON_C=OFF -DUSE_FFMPEG=OFF -DUSE_LUA=OFF \
    -DBUILD_GL=OFF -DBUILD_GLES2=OFF -DBUILD_GLES3=OFF -DUSE_EPOXY=OFF -DUSE_DISCORD_RPC=OFF
ninja -C mgba/build-linux mgba

# 2. the app, compiled with the same defines the library was built with
DEFS=$(grep -m1 -A3 "CMakeFiles/mgba.dir/src/core/core.c.o:" mgba/build-linux/build.ninja | grep DEFINES | sed 's/ *DEFINES = //')
gcc -O2 -std=c11 $DEFS -Imgba/include -Imgba/build-linux/include -Imgba/src $(sdl2-config --cflags) \
    bbcoop.c -o bbcoop mgba/build-linux/libmgba.a $(sdl2-config --libs) -lm -pthread
echo "Built ./bbcoop"
