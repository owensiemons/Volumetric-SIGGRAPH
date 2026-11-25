cmake_minimum_required(VERSION 3.21)
project(Volumetric-SIGGRAPH CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(PNG REQUIRED)

add_executable(Volumetric-SIGGRAPH src/main.cpp src/shader_class.cpp src/common.cpp)

target_sources(Volumetric-SIGGRAPH PRIVATE src/shader_class.hpp src/common.hpp)

target_link_libraries(Volumetric-SIGGRAPH PRIVATE glfw glad::glad PNG::PNG)

add_custom_command(TARGET Volumetric-SIGGRAPH POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Volumetric-SIGGRAPH>/src
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/vert.glsl
        ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/frag.glsl
        $<TARGET_FILE_DIR:Volumetric-SIGGRAPH>/src
)
