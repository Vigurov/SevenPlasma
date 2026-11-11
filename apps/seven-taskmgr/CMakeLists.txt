cmake_minimum_required(VERSION 3.16)
project(seven-taskmgr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets DBus LinguistTools)
find_package(KF6WindowSystem REQUIRED)

add_executable(seven-taskmgr main.cpp)
target_link_libraries(seven-taskmgr PRIVATE Qt6::Widgets Qt6::DBus KF6::WindowSystem)

install(TARGETS seven-taskmgr RUNTIME DESTINATION bin)

target_include_directories(seven-taskmgr PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../seven-common)

# translations: the code is English, i18n/<lang>.ts are built into the program as :/i18n/<lang>.qm
qt_add_translations(seven-taskmgr TS_FILES i18n/uk.ts RESOURCE_PREFIX /i18n LUPDATE_TARGET seven-taskmgr_lupdate_unused)
