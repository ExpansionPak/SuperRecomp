cmake_minimum_required(VERSION 3.14)

project(SuperRecomp VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCE_FILES
    src/boot.cpp
)

add_executable(SuperRecomp ${SOURCE_FILES})

target_include_directories(SuperRecomp PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
