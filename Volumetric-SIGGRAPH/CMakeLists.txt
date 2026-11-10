cmake_minimum_required(VERSION 3.21)
project(VolumetricSIGGRAPH CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)

add_executable(Volumetric-SIGGRAPH src/main.cpp)
target_link_libraries(Volumetric-SIGGRAPH PRIVATE glfw glad::glad)

add_custom_command(TARGET Volumetric-SIGGRAPH POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Volumetric-SIGGRAPH>/src
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vert.glsl
        ${CMAKE_CURRENT_SOURCE_DIR}/src/frag.glsl
        $<TARGET_FILE_DIR:Volumetric-SIGGRAPH>/src
)
