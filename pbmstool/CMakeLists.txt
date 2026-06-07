cmake_minimum_required(VERSION 3.14)
project(pbmstool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Curses REQUIRED)

add_executable(pbmstool
    src/protocol.cpp
    src/serial.cpp
    src/bms.cpp
    src/tui.cpp
    src/main.cpp
)
target_include_directories(pbmstool PRIVATE src ${CURSES_INCLUDE_DIR})
target_link_libraries(pbmstool PRIVATE ${CURSES_LIBRARIES})
target_compile_options(pbmstool PRIVATE -Wall -Wextra)
