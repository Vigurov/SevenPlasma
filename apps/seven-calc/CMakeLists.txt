cmake_minimum_required(VERSION 3.16)
project(seven-calc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets LinguistTools)

add_executable(seven-calc main.cpp)
target_link_libraries(seven-calc PRIVATE Qt6::Widgets)

install(TARGETS seven-calc RUNTIME DESTINATION bin)

target_include_directories(seven-calc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../seven-common)

# translations: the code is English, i18n/<lang>.ts are built into the program as :/i18n/<lang>.qm
qt_add_translations(seven-calc TS_FILES i18n/uk.ts RESOURCE_PREFIX /i18n LUPDATE_TARGET seven-calc_lupdate_unused)
