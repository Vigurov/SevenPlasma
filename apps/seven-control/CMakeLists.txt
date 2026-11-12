cmake_minimum_required(VERSION 3.16)
project(seven-control LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network DBus Svg PrintSupport UiPlugin LinguistTools)
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
# On-Screen Keyboard on Wayland: KWin's fake input (optional: the X11 build types with XTest)
find_package(KWayland QUIET)

add_executable(seven-control main.cpp sevenrunner.cpp win7safelyremove.h)
target_include_directories(seven-control PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../seven-common)
target_link_libraries(seven-control PRIVATE Qt6::Widgets Qt6::Network Qt6::DBus Qt6::Svg Qt6::PrintSupport Fontconfig::Fontconfig Freetype::Freetype PK::packagekitqt6 KF6::Baloo KF6::FileMetaData ${CMAKE_DL_LIBS} KF6::AuthCore KF6::ConfigCore KF6::Service KF6::Solid KF6::KIOFileWidgets KF6::KIOWidgets KF6::Service KF6::IconWidgets KF6::IdleTime KF6::StatusNotifierItem AeroQt::AeroQt xcb)

install(TARGETS seven-control RUNTIME DESTINATION bin)

# translations: the code is English, i18n/<lang>.ts are built into the program as :/i18n/<lang>.qm
# Device Manager is built in too (Computer Management): its translations as devmgr_<lang>
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../seven-devmgr/i18n/uk.ts ${CMAKE_CURRENT_BINARY_DIR}/devmgr_uk.ts COPYONLY)
# the shared headers (win7chrome.h) bring their own translations as common_<lang>
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../seven-common/i18n/common_uk.ts ${CMAKE_CURRENT_BINARY_DIR}/common_uk.ts COPYONLY)
qt_add_translations(seven-control TS_FILES ${CMAKE_CURRENT_BINARY_DIR}/common_uk.ts i18n/uk.ts ${CMAKE_CURRENT_BINARY_DIR}/devmgr_uk.ts RESOURCE_PREFIX /i18n LUPDATE_TARGET seven-control_lupdate_unused)
if(TARGET Plasma::KWaylandClient)
    target_link_libraries(seven-control PRIVATE Plasma::KWaylandClient)
    target_compile_definitions(seven-control PRIVATE WIN7_FAKEINPUT)
endif()
