cmake_minimum_required(VERSION 3.16)
project(seven-control LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network DBus Svg PrintSupport UiPlugin)
find_package(Fontconfig REQUIRED)
find_package(Freetype REQUIRED)
find_package(packagekitqt6 REQUIRED)
find_package(KF6Baloo REQUIRED)
find_package(KF6FileMetaData REQUIRED)
find_package(KF6IdleTime REQUIRED)
find_package(KF6StatusNotifierItem REQUIRED)
foreach(fw Auth Config Service Solid IconThemes FrameworkIntegration WindowSystem WidgetsAddons CoreAddons Completion KIO)
    find_package(KF6${fw} REQUIRED) # needed by AeroQt's exported target
endforeach()
find_package(AeroQt REQUIRED)

add_executable(seven-control main.cpp sevenrunner.cpp win7safelyremove.h)
target_include_directories(seven-control PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../seven-common)
target_link_libraries(seven-control PRIVATE Qt6::Widgets Qt6::Network Qt6::DBus Qt6::Svg Qt6::PrintSupport Fontconfig::Fontconfig Freetype::Freetype PK::packagekitqt6 KF6::Baloo KF6::FileMetaData ${CMAKE_DL_LIBS} KF6::AuthCore KF6::ConfigCore KF6::Service KF6::Solid KF6::KIOFileWidgets KF6::KIOWidgets KF6::Service KF6::IconWidgets KF6::IdleTime KF6::StatusNotifierItem AeroQt::AeroQt xcb)

install(TARGETS seven-control RUNTIME DESTINATION bin)
