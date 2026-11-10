cmake_minimum_required(VERSION 3.16)
project(seven-system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network UiPlugin LinguistTools)
find_package(KF6Config REQUIRED)
foreach(fw FrameworkIntegration WindowSystem WidgetsAddons CoreAddons Completion KIO)
    find_package(KF6${fw} REQUIRED) # needed by AeroQt's exported target
endforeach()
find_package(AeroQt REQUIRED)

add_executable(seven-system main.cpp)
target_include_directories(seven-system PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../seven-common)
target_link_libraries(seven-system PRIVATE Qt6::Widgets Qt6::Network KF6::ConfigCore AeroQt::AeroQt)

install(TARGETS seven-system RUNTIME DESTINATION bin)

# translations: the code is English, i18n/<lang>.ts are built into the program as :/i18n/<lang>.qm
qt_add_translations(seven-system TS_FILES i18n/uk.ts RESOURCE_PREFIX /i18n LUPDATE_TARGET seven-system_lupdate_unused)
