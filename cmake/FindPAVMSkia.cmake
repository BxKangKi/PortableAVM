# Locate a Skia tree built by scripts/build-windows.ps1 or scripts/build-linux.sh.
# Required cache variables:
#   PAVM_SKIA_ROOT - Skia source tree (contains include/core/SkCanvas.h)
#   PAVM_SKIA_OUT  - GN output directory (contains skia.lib/libskia.a)

include(FindPackageHandleStandardArgs)

find_path(PAVMSkia_INCLUDE_DIR
    NAMES include/core/SkCanvas.h
    HINTS "${PAVM_SKIA_ROOT}"
    NO_DEFAULT_PATH
)

if(WIN32)
    find_library(PAVMSkia_LIBRARY
        NAMES skia
        HINTS "${PAVM_SKIA_OUT}"
        NO_DEFAULT_PATH
    )
else()
    find_library(PAVMSkia_LIBRARY
        NAMES skia libskia.a
        HINTS "${PAVM_SKIA_OUT}"
        NO_DEFAULT_PATH
    )
endif()

find_package_handle_standard_args(PAVMSkia
    REQUIRED_VARS PAVMSkia_INCLUDE_DIR PAVMSkia_LIBRARY
)

if(PAVMSkia_FOUND AND NOT TARGET PAVMSkia::skia)
    add_library(PAVMSkia::skia STATIC IMPORTED)
    set_target_properties(PAVMSkia::skia PROPERTIES
        IMPORTED_LOCATION "${PAVMSkia_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${PAVMSkia_INCLUDE_DIR}"
    )

    if(WIN32)
        set_property(TARGET PAVMSkia::skia APPEND PROPERTY INTERFACE_LINK_LIBRARIES
            dwrite d2d1 dxgi gdi32 ole32 oleaut32 uuid user32 advapi32
        )
    elseif(APPLE)
        find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
        find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
        find_library(CORETEXT_FRAMEWORK CoreText REQUIRED)
        set_property(TARGET PAVMSkia::skia APPEND PROPERTY INTERFACE_LINK_LIBRARIES
            "${COREFOUNDATION_FRAMEWORK};${COREGRAPHICS_FRAMEWORK};${CORETEXT_FRAMEWORK}"
        )
    elseif(UNIX)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
        pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
        set_property(TARGET PAVMSkia::skia APPEND PROPERTY INTERFACE_LINK_LIBRARIES
            PkgConfig::FONTCONFIG PkgConfig::FREETYPE dl m
        )
    endif()
endif()
