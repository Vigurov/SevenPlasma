cmake_minimum_required(VERSION 3.16)
project(seven-ccc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Svg)
find_package(KF6WindowSystem REQUIRED)

add_executable(seven-ccc main.cpp)
target_link_libraries(seven-ccc PRIVATE Qt6::Widgets Qt6::Gui Qt6::Svg KF6::WindowSystem ${CMAKE_DL_LIBS})

install(TARGETS seven-ccc RUNTIME DESTINATION bin)
