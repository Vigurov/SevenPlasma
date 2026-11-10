cmake_minimum_required(VERSION 3.16)
project(seven-paint LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport LinguistTools)
add_executable(seven-paint main.cpp ribbon.h canvas.h)
target_link_libraries(seven-paint PRIVATE Qt6::Widgets Qt6::PrintSupport)
install(TARGETS seven-paint RUNTIME DESTINATION bin)

target_include_directories(seven-paint PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../seven-common)

# translations: the code is English, i18n/<lang>.ts are built into the program as :/i18n/<lang>.qm
qt_add_translations(seven-paint TS_FILES i18n/uk.ts RESOURCE_PREFIX /i18n LUPDATE_TARGET seven-paint_lupdate_unused)
