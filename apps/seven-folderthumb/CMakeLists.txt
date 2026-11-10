cmake_minimum_required(VERSION 3.16)
project(sevenfolderthumbnail LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(ECM REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(BUILD_SHARED_LIBS ON)
find_package(Qt6 REQUIRED COMPONENTS Gui)
find_package(KF6 REQUIRED COMPONENTS CoreAddons KIO)

kcoreaddons_add_plugin(sevenfolderthumbnail
    INSTALL_NAMESPACE "kf6/thumbcreator"
    SOURCES sevenfolderthumbnail.cpp compose.cpp
)
target_link_libraries(sevenfolderthumbnail Qt6::Gui KF6::CoreAddons KF6::KIOGui)

# offline preview tool for checking the look
add_executable(seven-folder-preview preview.cpp compose.cpp)
target_link_libraries(seven-folder-preview Qt6::Gui)
set_target_properties(sevenfolderthumbnail PROPERTIES PREFIX "")
