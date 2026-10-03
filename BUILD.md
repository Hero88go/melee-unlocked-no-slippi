# Building the Source Port

Windows x64 only. Two parts, built with two compilers, that end up in one folder:
`melee_source.exe` (the application) and `melee_game.dll` (the game, from the decompiled sources).

## Toolchains

- Visual Studio 2022 Build Tools (MSVC, x64) with a Windows 10 or 11 SDK (its `dxc.exe` is used).
- CMake 3.25 or newer, Python 3, Git.
- MinGW-w64 GCC 14 or newer with Ninja (a WinLibs build has both). The game needs GCC.

## 1. Game sources

The game is the public decompilation at a pinned commit plus this project's patch.

    git submodule update --init sourceport/extern/melee
    python tools/prepare_native_sources.py

(Outside a Git checkout: clone the decompilation into `sourceport/extern/melee`, check out the
commit named in `tools/prepare_native_sources.py`, then run that script.)

## 2. Two generated headers

The host reads the game's symbol addresses and a list of prototypes from `port/generated`. They are
generated from your own copy of the game: extract `main.dol` from your NTSC 1.02 ISO, then

    python port/recomp/recomp.py --no-slippi --dol <path to main.dol> --out port/generated

Only `hle_decls.h` and `guest_symbols.h` from that folder are used; nothing else in it is compiled.

## 3. The game library (GCC)

    cmake -S sourceport/game -B build-game -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_TOOLCHAIN_FILE=sourceport/cmake/mingw-w64-x86_64.cmake ^
      -DMELEE_MINGW_ROOT=<folder holding bin/gcc.exe> -DMELEE_PYTHON=<python.exe> -DMU_NO_SLIPPI=ON
    cmake --build build-game --target melee_game

This writes `melee_game.dll`, `melee_game.dbg` and `melee_game.snapexcl` into `build-game`.

## 4. The application (MSVC)

    cmake -S . -B build-host -G "Visual Studio 17 2022" -A x64
    cmake --build build-host --config Release --target melee_source

This writes `build-host/port/Release/melee_source.exe`. Copy the three `melee_game.*` files from
step 3 beside it (always all three together), and the `lang` folder.

Optional: NVIDIA Streamline, Intel XeSS and the NGX SDK are not part of this tree. With Streamline
unpacked under `port/third_party/streamline` and XeSS under `port/third_party/xess` the build picks
them up; `-DMELEE_ENABLE_DLSS5=ON` also needs the NGX SDK under `port/third_party/ngx`.

## 5. Running

Your own NTSC 1.02 ISO is never part of this tree. Pass it on the command line:

    melee_source.exe --iso <path to your Melee NTSC 1.02 ISO>

It can sit anywhere; nothing is written to it.
