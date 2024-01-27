cmake_minimum_required(VERSION 3.10)

project(psfiles)

set(CMAKE_CXX_FLAGS "--std=c++20 -Wall -Wextra -Wpedantic -Werror")

set(SOURCES
    args.cpp
    column.cpp
    main.cpp
    input.cpp
    output.cpp
    tracer.cpp)

add_executable(${PROJECT_NAME}
               ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
                           ${CMAKE_SOURCE_DIR})

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
