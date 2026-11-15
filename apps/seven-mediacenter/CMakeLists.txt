cmake_minimum_required(VERSION 3.16)
project(seven-mediacenter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Gui Qml Quick Multimedia Concurrent LinguistTools)

add_executable(seven-mediacenter main.cpp qml.qrc)
target_link_libraries(seven-mediacenter PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Multimedia Qt6::Concurrent)

install(TARGETS seven-mediacenter RUNTIME DESTINATION bin)

target_include_directories(seven-mediacenter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../seven-common)

# translations: the code is English (qsTranslate("Win7", ...) in QML), i18n/<lang>.ts are built in as :/i18n/<lang>.qm
qt_add_translations(seven-mediacenter TS_FILES i18n/uk.ts RESOURCE_PREFIX /i18n LUPDATE_TARGET seven-mediacenter_lupdate_unused)
