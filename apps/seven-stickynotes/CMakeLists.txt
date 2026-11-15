cmake_minimum_required(VERSION 3.16)
project(seven-stickynotes LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network LinguistTools)
find_package(KF6WindowSystem REQUIRED)

add_executable(seven-stickynotes main.cpp)
target_link_libraries(seven-stickynotes PRIVATE Qt6::Widgets Qt6::Network KF6::WindowSystem)

install(TARGETS seven-stickynotes RUNTIME DESTINATION bin)

target_include_directories(seven-stickynotes PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../seven-common)

# translations: the code is English, i18n/<lang>.ts are built into the program as :/i18n/<lang>.qm
qt_add_translations(seven-stickynotes TS_FILES i18n/uk.ts RESOURCE_PREFIX /i18n LUPDATE_TARGET seven-stickynotes_lupdate_unused)
