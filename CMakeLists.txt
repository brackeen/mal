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
else() # Assume iOS
    set(IOS TRUE)
    set(MAL_SRC ${MAL_SRC} src/mal_platform_ios.m src/mal_audio_coreaudio.h)
    set(CMAKE_OSX_SYSROOT "iphoneos")
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
elseif (IOS)
    target_link_libraries(mal "-framework Foundation -framework AudioToolbox -framework AVFoundation")
    set_target_properties(mal PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/MAL.build/lib # For Archiving
        XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator appletvos appletvsimulator"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET 8.0
        XCODE_ATTRIBUTE_TVOS_DEPLOYMENT_TARGET 9.0
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    )
endif()

if (MAL_BUILD_EXAMPLE)
    add_subdirectory(example)
endif()
