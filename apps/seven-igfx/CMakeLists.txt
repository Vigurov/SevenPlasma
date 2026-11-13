cmake_minimum_required(VERSION 3.16)
project(seven-igfx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui DBus LinguistTools)

add_executable(seven-igfx main.cpp)
# the backend is shared with seven-ccc (displays, gamma, power, driconf)
target_include_directories(seven-igfx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../seven-ccc)
target_link_libraries(seven-igfx PRIVATE Qt6::Widgets Qt6::Gui Qt6::DBus ${CMAKE_DL_LIBS})

install(TARGETS seven-igfx RUNTIME DESTINATION bin)

target_include_directories(seven-igfx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../seven-common)

# translations: the code is English, i18n/<lang>.ts are built into the program as :/i18n/<lang>.qm
qt_add_translations(seven-igfx TS_FILES i18n/uk.ts RESOURCE_PREFIX /i18n LUPDATE_TARGET seven-igfx_lupdate_unused)
