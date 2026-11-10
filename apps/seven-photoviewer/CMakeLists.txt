cmake_minimum_required(VERSION 3.16)
project(seven-photoviewer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport LinguistTools)
find_package(KF6KIO REQUIRED)
find_package(KF6Service REQUIRED)
add_executable(seven-photoviewer main.cpp)
target_link_libraries(seven-photoviewer PRIVATE Qt6::Widgets Qt6::PrintSupport KF6::KIOGui KF6::KIOWidgets KF6::Service)
install(TARGETS seven-photoviewer RUNTIME DESTINATION bin)

target_include_directories(seven-photoviewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../seven-common)

# translations: the code is English, i18n/<lang>.ts are built into the program as :/i18n/<lang>.qm
qt_add_translations(seven-photoviewer TS_FILES i18n/uk.ts RESOURCE_PREFIX /i18n LUPDATE_TARGET seven-photoviewer_lupdate_unused)
