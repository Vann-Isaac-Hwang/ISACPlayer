cmake_minimum_required(VERSION 3.16)

project(NcmTrackProbe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

get_filename_component(ISACPLAYER_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_executable(ncm-track-probe
    main.cpp
    "${ISACPLAYER_ROOT}/format_plugins/ncm/NcmReader.cpp"
    "${ISACPLAYER_ROOT}/format_plugins/ncm/third_party/aes.cpp"
)

target_include_directories(ncm-track-probe PRIVATE
    "${ISACPLAYER_ROOT}/format_plugins/ncm"
    "${ISACPLAYER_ROOT}/format_plugins/ncm/third_party"
    "${ISACPLAYER_ROOT}/format_sdk/include"
)

target_link_libraries(ncm-track-probe PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
)
