if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    cmake_minimum_required(VERSION 3.6.0)
else()
    cmake_minimum_required(VERSION 3.2.0)
endif()

function(set_glfw_app_properties TARGET_NAME)
    if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
        set_target_properties(${TARGET_NAME} PROPERTIES
            XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET 10.7
        )
        set_target_properties(glfw PROPERTIES
            XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET 10.7
        )
    elseif (CMAKE_SYSTEM_NAME MATCHES "Windows")
        set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${TARGET_NAME})
        # Hide the console
        set_target_properties(${TARGET_NAME} PROPERTIES
                              LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
    elseif (CMAKE_SYSTEM_NAME MATCHES "Linux")
        set_target_properties(${TARGET_NAME} PROPERTIES COMPILE_FLAGS "-std=gnu99")
    endif()

    # Copy assets to build dir
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${TARGET_NAME}>
    )
endfunction(set_glfw_app_properties)

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

    # Main example
    set(GLFM_APP_TARGET_NAME mal_example)
    file(GLOB GLFM_APP_SRC src/main.c src/ok_wav.c)
    source_group("src" FILES ${GLFM_APP_SRC})
    include(GLFMAppTarget)

    # Stress Test
    set(GLFM_APP_TARGET_NAME mal_stress_test)
    file(GLOB GLFM_APP_SRC src/stress_test.c)
    source_group("src" FILES ${GLFM_APP_SRC})
    include(GLFMAppTarget)
elseif ((CMAKE_SYSTEM_NAME MATCHES "Darwin") OR (CMAKE_SYSTEM_NAME MATCHES "Windows") OR (CMAKE_SYSTEM_NAME MATCHES "Linux"))
    add_definitions(-DMAL_EXAMPLE_WITH_GLFW)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build the GLFW example programs")
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "Build the GLFW test programs")
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build the GLFW documentation")
    set(GLFW_INSTALL OFF CACHE BOOL "Generate installation target")
    add_subdirectory(glfw)
    include_directories("glfw/deps") # For glad
    include_directories("glfw/include")

    # Main example
    file(GLOB mal_example_files "src/main.c" "src/ok_wav.c" "src/ok_wav.h" "src/file_compat.h" "glfw/deps/glad.c")
    add_executable(mal_example ${mal_example_files})
    source_group("src" FILES ${mal_example_files})
    target_link_libraries(mal_example mal glfw)
    set_glfw_app_properties(mal_example)

    # Stress test
    file(GLOB mal_stress_test_files "src/stress_test.c" "glfw/deps/glad.c")
    add_executable(mal_stress_test ${mal_stress_test_files})
    source_group("src" FILES ${mal_stress_test_files})
    target_link_libraries(mal_stress_test mal glfw)
    set_glfw_app_properties(mal_stress_test)
else()
    message(FATAL_ERROR "Unsupported CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")
endif()
