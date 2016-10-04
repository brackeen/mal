
cmake_minimum_required(VERSION 3.4.1)

# GLFM

# Set NDEBUG for android_native_app_glue to remove some superfluous logging
SET_SOURCE_FILES_PROPERTIES( ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
                              PROPERTIES COMPILE_FLAGS "-DNDEBUG" )
INCLUDE_DIRECTORIES( ../../../glfm/include/
                    ${ANDROID_NDK}/sources/android/native_app_glue/ )
ADD_LIBRARY( glfm STATIC ../../../glfm/src/glfm_platform_android.c
             ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c )

# MAL

INCLUDE_DIRECTORIES( ../../../../../mal/include/ )
ADD_LIBRARY( mal STATIC ../../../../../mal/src/mal_platform_android.c )

# Example code

FILE( GLOB mal_example_src ../../../src/*.c )
ADD_LIBRARY( mal_example SHARED ${mal_example_src} )

# NDK libs

FIND_LIBRARY( log-lib log )
FIND_LIBRARY( android-lib android )
FIND_LIBRARY( EGL-lib EGL )
FIND_LIBRARY( GLESv2-lib GLESv2 )
FIND_LIBRARY( OpenSLES-lib OpenSLES )

# Link

TARGET_LINK_LIBRARIES( mal_example
                       glfm
                       mal
                       ${log-lib}
                       ${android-lib}
                       ${EGL-lib}
                       ${GLESv2-lib}
                       ${OpenSLES-lib}
                        )
