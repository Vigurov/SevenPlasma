cmake_minimum_required(VERSION 3.16)
project(seven-taskmgr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(KF6WindowSystem REQUIRED)

add_executable(seven-taskmgr main.cpp)
target_link_libraries(seven-taskmgr PRIVATE Qt6::Widgets KF6::WindowSystem)

install(TARGETS seven-taskmgr RUNTIME DESTINATION bin)
