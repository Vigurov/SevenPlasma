cmake_minimum_required(VERSION 3.16)
project(seven-wmp LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Network Concurrent DBus LinguistTools)
find_package(KF6FileMetaData REQUIRED)
find_package(KF6KIO REQUIRED)
find_package(KF6Solid REQUIRED)
add_executable(seven-wmp main.cpp engine.h library.h libraryview.h nowplaying.h transport.h res.h mpris.h options.h skin.h autoplaylist.h devices.h eq.h)
target_link_libraries(seven-wmp PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets Qt6::Network Qt6::Concurrent Qt6::DBus
    KF6::FileMetaData KF6::KIOGui KF6::KIOWidgets KF6::Solid)
install(TARGETS seven-wmp RUNTIME DESTINATION bin)

target_include_directories(seven-wmp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../seven-common)

# translations: the code is English, i18n/<lang>.ts are built into the program as :/i18n/<lang>.qm
qt_add_translations(seven-wmp TS_FILES i18n/uk.ts RESOURCE_PREFIX /i18n LUPDATE_TARGET seven-wmp_lupdate_unused)
