cmake_minimum_required(VERSION 2.8.7)

project(MAL C)

option(MAL_BUILD_EXAMPLE "Build the MAL example" OFF)

set(MAL_HEADERS include/mal.h)
set(MAL_SRC src/mal_audio_abstract.h src/ok_lib.h)

if (CMAKE_SYSTEM_NAME MATCHES "Emscripten")
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
    message(FATAL_ERROR "CMAKE_SYSTEM_NAME must be set to Emscripten, Android, or Darwin. Current value is ${CMAKE_SYSTEM_NAME}")
endif()

add_library(mal ${MAL_SRC} ${MAL_HEADERS})
target_include_directories(mal PUBLIC include)
target_include_directories(mal PRIVATE src)

source_group(include FILES ${MAL_HEADERS})
source_group(src FILES ${MAL_SRC})

if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    set_target_properties(mal PROPERTIES COMPILE_FLAGS "-Weverything -Wwrite-strings -Wno-padded -Wno-covered-switch-default ${MAL_COMPILE_FLAGS}")
elseif (CMAKE_C_COMPILER_ID MATCHES "GNU")
    set_target_properties(mal PROPERTIES COMPILE_FLAGS "-Wall -Wextra -Wwrite-strings ${MAL_COMPILE_FLAGS}")
elseif (CMAKE_C_COMPILER_ID MATCHES "MSVC")
    set_target_properties(mal PROPERTIES COMPILE_FLAGS "/Wall ${MAL_COMPILE_FLAGS}")
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
        find_library(COREAUDIO_FRAMEWORK CoreAudio)
        mark_as_advanced(AUDIOTOOLBOX_FRAMEWORK)
        mark_as_advanced(COREAUDIO_FRAMEWORK)
        list(APPEND mal_libraries "${AUDIOTOOLBOX_FRAMEWORK}" "${COREAUDIO_FRAMEWORK}")
        target_link_libraries(mal ${mal_libraries})
    endif()
endif()

if (MAL_BUILD_EXAMPLE)
    add_subdirectory(example)
endif()
