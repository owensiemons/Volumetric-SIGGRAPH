cmake_minimum_required(VERSION 3.10)
project(volumetric)

# Require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find GLFW and OpenGL
find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)






# Create the executable from the src directory
add_executable(volumetric src/main.cpp src/glad.c)


target_include_directories(volumetric PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link the libraries
target_link_libraries(volumetric glfw OpenGL::GL)
