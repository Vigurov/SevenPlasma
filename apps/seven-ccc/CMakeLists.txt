cmake_minimum_required(VERSION 3.16)
project(seven-ccc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Svg DBus LinguistTools)
find_package(KF6WindowSystem REQUIRED)

add_executable(seven-ccc main.cpp)
target_link_libraries(seven-ccc PRIVATE Qt6::Widgets Qt6::Gui Qt6::Svg Qt6::DBus KF6::WindowSystem ${CMAKE_DL_LIBS})

install(TARGETS seven-ccc RUNTIME DESTINATION bin)

target_include_directories(seven-ccc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../seven-common)

# translations: the code is English, i18n/<lang>.ts are built into the program as :/i18n/<lang>.qm
qt_add_translations(seven-ccc TS_FILES i18n/uk.ts RESOURCE_PREFIX /i18n LUPDATE_TARGET seven-ccc_lupdate_unused)
