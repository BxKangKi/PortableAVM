cmake_minimum_required(VERSION 3.24)

project(PortableAVM
    VERSION 0.3.46
    DESCRIPTION "Portable Android Virtual Device manager"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CTest)
include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    # Keep the packaged Windows executable self-contained. Third-party notices
    # are still installed; static linkage does not remove license obligations.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W4 /permissive- /utf-8 /EHsc /Zc:__cplusplus)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

option(PAVM_BUILD_GUI "Build the Skia/SDL3 desktop application" ON)
option(PAVM_BUILD_TESTS "Build core tests" ON)
option(PAVM_USE_BUNDLED_SDL "Build SDL3 from a supplied/downloaded source tree" ON)
option(PAVM_USE_BUNDLED_CURL "Build curl from a supplied/downloaded source tree" ${WIN32})

set(PAVM_SKIA_ROOT "" CACHE PATH "Path to the Skia source root")
set(PAVM_SKIA_OUT "" CACHE PATH "Path to the Skia GN output directory containing skia.lib/libskia.a")
set(PAVM_SDL_SOURCE_DIR "" CACHE PATH "Optional local SDL3 source tree")
set(PAVM_CURL_SOURCE_DIR "" CACHE PATH "Optional local curl source tree")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(Threads REQUIRED)

if(PAVM_USE_BUNDLED_CURL)
    set(PAVM_SAVED_BUILD_TESTING "${BUILD_TESTING}")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
    set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(HTTP_ONLY ON CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
    if(WIN32)
        set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
        set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
    endif()

    if(PAVM_CURL_SOURCE_DIR)
        if(NOT EXISTS "${PAVM_CURL_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "PAVM_CURL_SOURCE_DIR does not contain curl: ${PAVM_CURL_SOURCE_DIR}")
        endif()
        add_subdirectory("${PAVM_CURL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/curl-build" EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(curl
            GIT_REPOSITORY https://github.com/curl/curl.git
            GIT_TAG curl-8_11_1
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(curl)
    endif()
    set(BUILD_TESTING "${PAVM_SAVED_BUILD_TESTING}" CACHE BOOL "" FORCE)
    set(PAVM_CURL_TARGET libcurl)
else()
    find_package(CURL REQUIRED)
    set(PAVM_CURL_TARGET CURL::libcurl)
endif()

set(PAVM_CORE_SOURCES
    src/core/AdbManager.cpp
    src/core/ApkInspector.cpp
    src/core/ArchiveExtractor.cpp
    src/core/AvdManager.cpp
    src/core/Config.cpp
    src/core/EmulatorManager.cpp
    src/core/HostInfo.cpp
    src/core/HttpClient.cpp
    src/core/Logger.cpp
    src/core/LanguageManager.cpp
    src/core/PortablePaths.cpp
    src/core/Process.cpp
    src/core/RepositoryParser.cpp
    src/core/SdkManager.cpp
    src/core/Sha1.cpp
    src/core/StringUtil.cpp
    src/core/TaskQueue.cpp
)

add_library(pavm_core STATIC ${PAVM_CORE_SOURCES})
target_include_directories(pavm_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pavm_core PUBLIC ${PAVM_CURL_TARGET} Threads::Threads)
if(WIN32)
    target_compile_definitions(pavm_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_libraries(pavm_core PUBLIC bcrypt crypt32 ws2_32 winhttp)
endif()

add_library(pavm_platform STATIC
    src/platform/NativeDialogs.cpp
    src/platform/SingleInstance.cpp
)
target_include_directories(pavm_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pavm_platform PUBLIC pavm_core)
if(WIN32)
    target_compile_definitions(pavm_platform PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_libraries(pavm_platform PUBLIC comdlg32 shell32 user32 ole32)
elseif(UNIX)
    target_link_libraries(pavm_platform PUBLIC dl)
endif()

if(PAVM_BUILD_GUI)
    find_package(PAVMSkia REQUIRED)

    if(PAVM_USE_BUNDLED_SDL)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

        if(PAVM_SDL_SOURCE_DIR)
            if(NOT EXISTS "${PAVM_SDL_SOURCE_DIR}/CMakeLists.txt")
                message(FATAL_ERROR "PAVM_SDL_SOURCE_DIR does not contain SDL3: ${PAVM_SDL_SOURCE_DIR}")
            endif()
            add_subdirectory("${PAVM_SDL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/sdl3-build" EXCLUDE_FROM_ALL)
        else()
            FetchContent_Declare(SDL3
                GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
                GIT_TAG release-3.2.0
                GIT_SHALLOW TRUE
            )
            FetchContent_MakeAvailable(SDL3)
        endif()
        set(PAVM_SDL_TARGET SDL3::SDL3-static)
    else()
        find_package(SDL3 CONFIG REQUIRED)
        set(PAVM_SDL_TARGET SDL3::SDL3)
    endif()

    add_library(pavm_ui STATIC
        src/ui/ImmediateUi.cpp
        src/ui/SkiaRenderer.cpp
    )
    target_include_directories(pavm_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(pavm_ui PUBLIC PAVMSkia::skia ${PAVM_SDL_TARGET})

    set(PAVM_APP_SOURCES
        src/Application.cpp
        src/main.cpp
    )
    if(WIN32)
        set(PAVM_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/PortableAVM.ico")
        configure_file(resources/PortableAVM.rc.in "${CMAKE_CURRENT_BINARY_DIR}/PortableAVM.rc" @ONLY)
        list(APPEND PAVM_APP_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/PortableAVM.rc")
        add_executable(PortableAVM WIN32 ${PAVM_APP_SOURCES})
        target_compile_definitions(PortableAVM PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE SDL_MAIN_HANDLED)

    else()
        add_executable(PortableAVM ${PAVM_APP_SOURCES})
        target_compile_definitions(PortableAVM PRIVATE SDL_MAIN_HANDLED)
    endif()
    target_include_directories(PortableAVM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(PortableAVM PRIVATE pavm_core pavm_platform pavm_ui)
    add_custom_command(TARGET PortableAVM POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/Data/lang"
            "$<TARGET_FILE_DIR:PortableAVM>/Data/lang"
        COMMENT "Copy PortableAVM language files into Data/lang"
    )

    if(WIN32)
        install(TARGETS PortableAVM RUNTIME DESTINATION .)
    else()
        install(TARGETS PortableAVM RUNTIME DESTINATION .)
    endif()
endif()

if(PAVM_BUILD_TESTS AND BUILD_TESTING)
    add_executable(pavm_core_tests tests/core_tests.cpp)
    target_include_directories(pavm_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(pavm_core_tests PRIVATE pavm_core)
    add_test(NAME pavm_core_tests COMMAND pavm_core_tests)
endif()

install(DIRECTORY resources/Data/lang/ DESTINATION Data/lang)
