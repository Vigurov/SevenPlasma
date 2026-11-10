cmake_minimum_required(VERSION 3.16)
project(seven-nvidia LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui OpenGL OpenGLWidgets)

add_executable(seven-nvidia main.cpp)
target_include_directories(seven-nvidia PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../seven-ccc)
target_link_libraries(seven-nvidia PRIVATE Qt6::Widgets Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets)

install(TARGETS seven-nvidia RUNTIME DESTINATION bin)
