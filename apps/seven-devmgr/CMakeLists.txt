cmake_minimum_required(VERSION 3.16)
project(seven-devmgr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(seven-devmgr main.cpp)
target_link_libraries(seven-devmgr PRIVATE Qt6::Widgets)

install(TARGETS seven-devmgr RUNTIME DESTINATION bin)
