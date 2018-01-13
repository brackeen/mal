cmake_minimum_required(VERSION 2.8.12)

# Add Windows XP support. Must be set before project()
if ("${CMAKE_GENERATOR}" MATCHES "Visual Studio ([0-9]+)")
    if (${CMAKE_MATCH_1} EQUAL 12)
        # Visual Studio 2013
        set(CMAKE_GENERATOR_TOOLSET "v120_xp" CACHE STRING "CMAKE_GENERATOR_TOOLSET" FORCE)
        set(CMAKE_VS_PLATFORM_TOOLSET "v120_xp" CACHE STRING "CMAKE_VS_PLATFORM_TOOLSET" FORCE)
    elseif (${CMAKE_MATCH_1} EQUAL 14)
        # Visual Studio 2015
        set(CMAKE_GENERATOR_TOOLSET "v140_xp" CACHE STRING "CMAKE_GENERATOR_TOOLSET" FORCE)
        set(CMAKE_VS_PLATFORM_TOOLSET "v140_xp" CACHE STRING "CMAKE_VS_PLATFORM_TOOLSET" FORCE)
    elseif (${CMAKE_MATCH_1} EQUAL 15)
        # Visual Studio 2017
        set(CMAKE_GENERATOR_TOOLSET "v141_xp" CACHE STRING "CMAKE_GENERATOR_TOOLSET" FORCE)
        set(CMAKE_VS_PLATFORM_TOOLSET "v141_xp" CACHE STRING "CMAKE_VS_PLATFORM_TOOLSET" FORCE)
    else()
        message(FATAL_ERROR "Visual Studio version must be 12, 14, or 15")
    endif()
endif()

project(Mal)

option(MAL_BUILD_EXAMPLE "Build the MAL example" OFF)
if (CMAKE_C_COMPILER_ID MATCHES "MSVC")
    option(USE_MSVC_RUNTIME_LIBRARY_DLL "Use MSVC runtime library DLL" ON)
endif()

set(MAL_HEADERS include/mal.h)
set(MAL_SRC src/mal_audio_abstract.h src/ok_lib.h)

if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(MAL_SRC ${MAL_SRC} src/mal_platform_windows.cpp src/mal_audio_xaudio2.h)
elseif (CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(MAL_SRC ${MAL_SRC} src/mal_platform_linux.c src/mal_audio_pulseaudio.h)
    set(MAL_COMPILE_FLAGS "-std=c99")
elseif (CMAKE_SYSTEM_NAME MATCHES "Emscripten")
    set(MAL_SRC ${MAL_SRC} src/mal_platform_emscripten.c src/mal_audio_webaudio.h)
    if (CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(MAL_COMPILE_FLAGS -Wno-dollar-in-identifier-extension)
    endif()
elseif (CMAKE_SYSTEM_NAME MATCHES "Android")
    set(MAL_SRC ${MAL_SRC} src/mal_platform_android.c src/mal_audio_opensl.h)
elseif (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    if (IOS)
        set(MAL_SRC ${MAL_SRC} src/mal_platform_ios.m src/mal_audio_coreaudio.h)
        set(CMAKE_OSX_SYSROOT "iphoneos")
    else()
        set(MAL_SRC ${MAL_SRC} src/mal_platform_macos.m src/mal_audio_coreaudio.h)
    endif()
else()
    message(FATAL_ERROR "CMAKE_SYSTEM_NAME must be Windows, Darwin, Linux, Android, or Emscripten. Current value is ${CMAKE_SYSTEM_NAME}")
endif()

add_library(mal ${MAL_SRC} ${MAL_HEADERS})
target_include_directories(mal PUBLIC include)
target_include_directories(mal PRIVATE src)

source_group(include FILES ${MAL_HEADERS})
source_group(src FILES ${MAL_SRC})

if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    set_target_properties(mal PROPERTIES COMPILE_FLAGS "-Weverything -Wwrite-strings -Wno-padded -Wno-covered-switch-default ${MAL_COMPILE_FLAGS}")
elseif (CMAKE_C_COMPILER_ID MATCHES "GNU")
    # Disable unused-functions because of this GCC bug: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=64079
    set_target_properties(mal PROPERTIES COMPILE_FLAGS "-Wall -Wextra -Wwrite-strings -Wno-unused-function ${MAL_COMPILE_FLAGS}")
elseif (CMAKE_C_COMPILER_ID MATCHES "MSVC")
    # Disable `padding` warning
    set_target_properties(mal PROPERTIES COMPILE_FLAGS "/Wall /wd4820 /wd4324 ${MAL_COMPILE_FLAGS}")

    # Use XAudio2 2.7 for Windows 7 and XP compatability
    # Install DirectX SDK (June 2010) from https://www.microsoft.com/en-us/download/details.aspx?id=6812
    target_include_directories(mal PRIVATE "C:\\Program Files (x86)\\Microsoft DirectX SDK (June 2010)\\Include\\")

    # Statically link vcruntime.lib
    if (NOT USE_MSVC_RUNTIME_LIBRARY_DLL)
        foreach (flag CMAKE_CXX_FLAGS
                      CMAKE_CXX_FLAGS_DEBUG
                      CMAKE_CXX_FLAGS_RELEASE
                      CMAKE_CXX_FLAGS_MINSIZEREL
                      CMAKE_CXX_FLAGS_RELWITHDEBINFO)

            if (${flag} MATCHES "/MD")
                string(REGEX REPLACE "/MD" "/MT" ${flag} "${${flag}}")
            endif()
            if (${flag} MATCHES "/MDd")
                string(REGEX REPLACE "/MDd" "/MTd" ${flag} "${${flag}}")
            endif()

        endforeach()
    endif()
endif()

if (CMAKE_SYSTEM_NAME MATCHES "Android")
    find_library(log-lib log)
    find_library(android-lib android)
    find_library(OpenSLES-lib OpenSLES)
    target_link_libraries(mal ${log-lib} ${android-lib} ${OpenSLES-lib})
elseif (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    if (IOS)
        target_link_libraries(mal "-framework Foundation -framework AudioToolbox -framework AVFoundation")
        set_target_properties(mal PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/MAL.build/lib # For Archiving
            XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator appletvos appletvsimulator"
            XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET 8.0
            XCODE_ATTRIBUTE_TVOS_DEPLOYMENT_TARGET 9.0
            XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
        )
    else()
        find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
        find_library(AUDIOUNIT_FRAMEWORK AudioUnit)
        find_library(COREAUDIO_FRAMEWORK CoreAudio)
        mark_as_advanced(AUDIOTOOLBOX_FRAMEWORK)
        mark_as_advanced(AUDIOUNIT_FRAMEWORK)
        mark_as_advanced(COREAUDIO_FRAMEWORK)
        list(APPEND mal_libraries "${AUDIOTOOLBOX_FRAMEWORK}" "${AUDIOUNIT_FRAMEWORK}" "${COREAUDIO_FRAMEWORK}")
        target_link_libraries(mal ${mal_libraries})
        set_target_properties(mal PROPERTIES
            XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET 10.7
        )
    endif()
endif()

if (MAL_BUILD_EXAMPLE)
    add_subdirectory(example)
endif()
