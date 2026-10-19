# Building

## Dependencies

If your operating system has a package manager, we recommend installing the
dependencies with it.

In case you want to compile the dependencies yourself, you can find them,
except for [liblcf], in our [buildscripts] repository.


## Standard build with CMake

Building requirements:

- pkg-config (only on Linux)
- CMake 3.22 or newer

Step-by-step instructions:

    tar xf easyrpg-player-0.8.1.tar.xz    # unpack the tarball
    cd easyrpg-player-0.8.1               # enter in the package directory
    cmake . -DCMAKE_BUILD_TYPE=Release    # configure project
    cmake --build .                       # compile the executable
    sudo cmake --build . --target install # install system-wide

The tarball needs [liblcf] installed. CMake is the only supported way to build
Player. On Windows all dependencies must be installed with [vcpkg].


## From a git clone

[liblcf] is the `lib/liblcf` submodule and is built along with the Player:

    git clone --recurse-submodules https://github.com/oulianov/Player.git
    cd Player
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build

- A clone made without `--recurse-submodules` gets the submodule when CMake
  configures it (`git submodule update --init`).
- `PLAYER_BUILD_LIBLCF` is on by default in a git clone. Use
  `-DPLAYER_BUILD_LIBLCF=OFF` to build against an installed liblcf.

To build and run the unit tests:

    cmake --build build --target check


## macOS

Install the dependencies with [Homebrew]:

    brew install cmake ninja pkgconf sdl3 pixman libpng fmt freetype harfbuzz \
      lhasa nlohmann-json speexdsp libsamplerate mpg123 libsndfile libvorbis \
      opusfile wildmidi fluid-synth libxmp inih icu4c

Then build from a git clone as above. CMake needs two more search paths: ICU,
which Homebrew installs keg-only, and Homebrew's own prefix, where opusfile
finds the opus headers:

    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$(brew --prefix icu4c);$(brew --prefix)"
    cmake --build build

The result is `build/EasyRPG Player.app`. To run a game from the terminal:

    "build/EasyRPG Player.app/Contents/MacOS/EasyRPG Player" --project-path path/to/game --window


## Web (Emscripten)

The libraries come from the `emscripten` folder of [buildscripts]. Its
`0_build_everything.sh` builds them with Emscripten. On Linux and Intel Macs
it downloads the Emscripten SDK. On Apple Silicon Macs, install it first
(`brew install emscripten`). The README of buildscripts lists the other tools
it needs (autoconf, automake, libtool, meson...).

    git clone https://github.com/EasyRPG/buildscripts.git
    (cd buildscripts/emscripten && ./0_build_everything.sh)

Then, from a git clone of the Player next to buildscripts (`emcmake` comes with
Emscripten: `source buildscripts/emscripten/emsdk-portable/emsdk_env.sh` when
buildscripts downloaded it):

    emcmake cmake -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DPLAYER_ENABLE_TESTS=OFF \
      -DPLAYER_PREFIX_PATH_APPEND="$PWD/../buildscripts/emscripten" \
      -DPLAYER_FIND_ROOT_PATH_APPEND=ON \
      -DPLAYER_JS_BUILD_SHELL=ON
    cmake --build build-web

The result is `easyrpg-player.html`, `.js` and `.wasm` in `build-web`. A game
served next to them needs an `index.json` listing its files, like the one the
web export of the EasyRPG Editor writes.


## libretro core

Additional commands required before building:

    git submodule init   # Init submodules
    git submodule update # Clone libretro-common submodule

Invoke CMake with these additional parameters:

    -DPLAYER_TARGET_PLATFORM=libretro -DBUILD_SHARED_LIBS=ON|OFF

Set shared libs to ON or OFF depending on which type of libraries RetroArch
uses on the platform you are targeting.


## Android APK

Building requirements:

- Android SDK with NDK r21

Step-by-step instructions:

    tar xf easyrpg-player-0.8.1.tar.xz     # unpack the tarball
    cd easyrpg-player-0.8.1/builds/android # enter in the android directory
    ./gradlew -PtoolchainDirs="DIR1;DIR2" assembleRelease # create the APK

Replace ``DIR1`` etc. with the path to the player dependencies. You can use
the scripts in the ``android`` folder of our [buildscripts] to compile them.

To pass additional CMake arguments use ``-PcmakeOptions``:

    -PcmakeOptions="-DSOME_OPTION1=ON -DSOME_OPTION2=OFF"

The unsigned APK is stored in:

    app/build/outputs/apk/release/app-release-unsigned.apk


## Nintendo and Sony Homebrew ports (Wii (U), 3DS, Switch, PSVita/PSTV)

Building requirements:

- devkitPPC for Wii and Wii U
- devkitARM for 3DS
- devkitA64 for Switch
- vitasdk for PSVita/PSTV

You can get them at [devkitpro.org] and [vitasdk.org].

Invoke CMake with these additional parameters:

    -DCMAKE_TOOLCHAIN_FILE=<DEVKITPRO>/cmake/3DS|Switch|Wii|WiiU.cmake
                         (or <VITASDK>/share/vita.toolchain.cmake)

Switch, Wii U and 3DS support shipping games, use these parameters:

    -DPLAYER_BUNDLE=ON -DPLAYER_BUNDLE_PATH=path/to/myGame


[buildscripts]: https://github.com/EasyRPG/buildscripts
[Homebrew]: https://brew.sh
[liblcf]: https://github.com/EasyRPG/liblcf
[vcpkg]: https://github.com/Microsoft/vcpkg
[devkitpro.org]: https://devkitpro.org/wiki/Getting_Started
[vitasdk.org]: https://vitasdk.org
