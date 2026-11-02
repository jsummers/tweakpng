TweakPNG
========

A low-level tool for viewing and editing the chunks inside PNG, MNG and JNG
files, for Linux, macOS and Windows.

This is a port of Jason Summers' [TweakPNG](https://entropymine.com/jason/tweakpng/)
from the Win32 API to Qt, built with Qt 6 or, failing that, Qt 5.15.

For usage instructions, see [resources/tweakpng.txt](resources/tweakpng.txt),
which is also available from Help > Contents.

Downloads
---------

Tagged releases on the Releases page provide:

| Platform | File |
|----------|------|
| Linux x86-64 | `.AppImage` |
| macOS (Apple silicon and Intel) | `.app` in a `.zip` or `.dmg` |
| Windows x64 | `-setup.exe` installer, or a portable `.zip` |

The macOS and Windows builds are not signed, so the system warns about them on
first launch. On macOS, Control-click the app and choose Open. On Windows,
choose More info, then Run anyway.

Building
--------

You need CMake 3.16 or newer, a C++17 compiler, and Qt 6 or Qt 5.15 with the
Core, Gui, Widgets and Test modules. On macOS 26, Qt 6 must be version 6.9.3 or
newer, because older versions link a framework that macOS 26 removed. zlib and libpng are taken from the system
if present; otherwise CMake downloads and builds them.

    cmake -S . -B build
    cmake --build build
    ctest --test-dir build --output-on-failure

CMake prefers Qt 6. To choose, add `-DTWEAKPNG_QT_VERSION=5` or `=6`.

To turn off the tests, add `-DTWEAKPNG_BUILD_TESTS=OFF`. To forbid downloads,
add `-DTWEAKPNG_FETCH_DEPS=OFF`. To always build a library from source, add
`-DTWEAKPNG_BUNDLED_ZLIB=ON` or `-DTWEAKPNG_BUNDLED_PNG=ON`.

On Debian or Ubuntu:

    sudo apt install cmake g++ qt6-base-dev libgl-dev libpng-dev zlib1g-dev

Layout
------

- `src/core`: the file model, chunk descriptions, ICC profile reader and PNG
  decoder. It has no user interface code and is what `tests/test_core.cpp` tests.
- `src/ui`: the main window, chunk editors, palette editor and image viewer.
- `tests`: the test suite, which runs without a display.
- `packaging`: the desktop entry, app bundle metadata and Windows installer.
- `.github/workflows`: `ci.yml` builds and tests every push on all three
  platforms with both Qt versions. `release.yml` builds the packages when a
  `v*` tag is pushed.

Making a release
----------------

    git tag v1.4.7
    git push origin v1.4.7

Update the version in `CMakeLists.txt`, `src/core/twpng.h` and
`packaging/windows/tweakpng.rc` first.

License
-------

GNU General Public License, version 3 or later. See [COPYING.txt](COPYING.txt).
