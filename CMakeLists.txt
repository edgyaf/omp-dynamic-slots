cmake_minimum_required(VERSION 3.19)
project(advanced-query LANGUAGES C CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(sdk)
add_subdirectory(network)

include_directories(
    .
    pawn/source
    pawn/source/linux
    pawn-natives
)

add_definitions(
    -DHAVE_STDINT_H=1
    -DPAWN_CELL_SIZE=32
)

add_library(${PROJECT_NAME} SHARED main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE
    OMP-SDK
    OMP-Network
)

