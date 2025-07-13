# CMake minimum version
cmake_minimum_required(VERSION 3.10)

# Project name
project(RainMemoPool)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Include file
file(GLOB INCLUDE_FILES "*.h")

# Source files
file(GLOB SRC_FILES "*.cpp")
file(GLOB TEST_FILES "*.cpp")

# Add the executable for the main target
add_executable(${PROJECT_NAME} ${SRC_FILES} ${TEST_FILES})

# Add compile options
target_compile_options(${PROJECT_NAME} PRIVATE -g -pthread)

# Link libraries
target_link_libraries(${PROJECT_NAME} pthread)
