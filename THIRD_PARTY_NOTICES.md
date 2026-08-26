# Third-party notices

PortableAVM does not change the ownership or license of third-party components.

## Skia

Used as the CPU raster rendering engine. Source: `https://skia.googlesource.com/skia.git`. The build script copies the checked-out `LICENSE` file to the package. Skia may fetch additional source dependencies through its official `tools/git-sync-deps`; review their licenses before redistribution.

## SDL3

Used for native windows, input, clipboard, and DPI information. Source: `https://github.com/libsdl-org/SDL.git`. The build script copies `LICENSE.txt` to the package.

## curl

Used for HTTPS downloads. Source: `https://github.com/curl/curl.git`. Windows builds use the Windows Schannel backend and copy `COPYING` to the package. Linux builds normally link to the distribution-provided libcurl and must retain the distribution's applicable notices.

## Android SDK tools

Not bundled. Installed by the end user from Google's official repository after an explicit consent step. Android SDK tools and packages remain subject to their own licenses and terms.

## JDK

Not bundled. Imported by the end user from a separately obtained JDK distribution, whose license remains applicable.
