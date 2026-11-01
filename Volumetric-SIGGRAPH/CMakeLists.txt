cmake_minimum_required(VERSION 3.21)
project(VolumetricSIGGRAPH CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)

add_executable(Volumetric-SIGGRAPH main.cpp)
target_link_libraries(Volumetric-SIGGRAPH PRIVATE glfw glad::glad)

# Let main.cpp find the shader files no matter where the exe is run from
target_compile_definitions(Volumetric-SIGGRAPH PRIVATE
    SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders/")
