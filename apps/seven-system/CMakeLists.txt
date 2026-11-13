cmake_minimum_required(VERSION 3.16)
project(seven-system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# only a launcher: System is a page of seven-control (win7system.h)
find_package(Qt6 REQUIRED COMPONENTS Core)

add_executable(seven-system main.cpp)
target_link_libraries(seven-system PRIVATE Qt6::Core)

install(TARGETS seven-system RUNTIME DESTINATION bin)
