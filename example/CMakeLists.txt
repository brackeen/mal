cmake_minimum_required(VERSION 3.6.0) # Might run on earlier versions. Probably requires 3.4 or 3.5

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
