cmake_minimum_required(VERSION 3.16)
project(seven-dvdmaker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Xml Concurrent UiPlugin LinguistTools)
foreach(fw Auth Config Service Solid IconThemes FrameworkIntegration WindowSystem WidgetsAddons CoreAddons Completion KIO)
    find_package(KF6${fw} REQUIRED) # needed by AeroQt's exported target
endforeach()
find_package(AeroQt REQUIRED)

add_executable(seven-dvdmaker main.cpp menurender.h burner.h)
target_include_directories(seven-dvdmaker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../seven-common)
target_link_libraries(seven-dvdmaker PRIVATE Qt6::Widgets Qt6::Xml Qt6::Concurrent AeroQt::AeroQt)

install(TARGETS seven-dvdmaker RUNTIME DESTINATION bin)

# translations: the code is English, i18n/<lang>.ts are built into the program as :/i18n/<lang>.qm
qt_add_translations(seven-dvdmaker TS_FILES i18n/uk.ts RESOURCE_PREFIX /i18n LUPDATE_TARGET seven-dvdmaker_lupdate_unused)
