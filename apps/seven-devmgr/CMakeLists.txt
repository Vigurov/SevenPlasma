cmake_minimum_required(VERSION 3.16)
project(seven-devmgr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets LinguistTools)

add_executable(seven-devmgr main.cpp)
target_link_libraries(seven-devmgr PRIVATE Qt6::Widgets)

install(TARGETS seven-devmgr RUNTIME DESTINATION bin)

target_include_directories(seven-devmgr PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../seven-common)

# translations: the code is English, i18n/<lang>.ts are built into the program as :/i18n/<lang>.qm
qt_add_translations(seven-devmgr TS_FILES i18n/uk.ts RESOURCE_PREFIX /i18n LUPDATE_TARGET seven-devmgr_lupdate_unused)
