cmake_minimum_required(VERSION 3.6.0)

if ((CMAKE_SYSTEM_NAME MATCHES "Emscripten") OR
    (CMAKE_SYSTEM_NAME MATCHES "Android") OR
    ((CMAKE_SYSTEM_NAME MATCHES "Darwin") AND IOS))

    add_definitions(-DMAL_EXAMPLE_WITH_GLFM)
    add_subdirectory(glfm)
    link_libraries(glfm)
    link_libraries(mal)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/glfm/example/cmake")

    # Common
    set(GLFM_APP_ORGANIZATION_IDENTIFIER "com.brackeen")
    set(GLFM_APP_VERSION "1.0")
    set(GLFM_APP_VERSION_ITERATION 1)
    set(GLFM_APP_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)

    # Example code
    set(GLFM_APP_TARGET_NAME mal_example)
    file(GLOB GLFM_APP_SRC src/*.c)
    source_group("src" FILES ${GLFM_APP_SRC})
    include(GLFMAppTarget)
elseif ((CMAKE_SYSTEM_NAME MATCHES "Darwin") OR (CMAKE_SYSTEM_NAME MATCHES "Windows"))
    add_definitions(-DMAL_EXAMPLE_WITH_GLFW)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build the GLFW example programs")
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "Build the GLFW test programs")
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build the GLFW documentation")
    set(GLFW_INSTALL OFF CACHE BOOL "Generate installation target")
    add_subdirectory(glfw)
    include_directories("glfw/deps") # For glad
    include_directories("glfw/include")

    file(GLOB mal_example_files "src/main.c" "src/ok_wav.c" "src/ok_wav.h" "glfw/deps/glad.c")
    add_executable(mal_example ${mal_example_files})
    source_group("src" FILES ${mal_example_files})
    target_link_libraries(mal_example mal glfw)

    if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
        set_target_properties(mal_example PROPERTIES
            XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET 10.7
        )
        set_target_properties(glfw PROPERTIES
            XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET 10.7
        )
    endif()

    if (CMAKE_C_COMPILER_ID MATCHES "MSVC")
        set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT mal_example)
        # Hide the console
        set_target_properties(mal_example PROPERTIES
                              LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
    endif()

    # Copy assets to build dir
    add_custom_command(TARGET mal_example POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:mal_example>
    )
else()
    message(FATAL_ERROR "Unsupported CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")
endif()
