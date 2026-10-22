cmake_minimum_required(VERSION 3.16)

project(ISACPlayer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Sql)
find_package(PkgConfig REQUIRED)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
pkg_check_modules(FFTW3 REQUIRED IMPORTED_TARGET fftw3)

# Public headers consumed by external DSP plugins. This subproject intentionally
# carries no Qt or engine implementation dependency.
add_subdirectory(plugin_sdk)

# 1. 纯 C++ 音频引擎核心库 (零 Qt 依赖)
add_library(isac_engine STATIC
    src/engine/graph/AudioBuffer.h
    src/engine/graph/IAudioNode.h
    src/engine/graph/GraphCompiler.h
    src/engine/graph/GraphCompiler.cpp
    src/engine/graph/SpscAudioRingBuffer.h
    src/engine/graph/AudioEngine.h
    src/engine/graph/AudioEngine.cpp
)
target_include_directories(isac_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin_sdk/include
)
if(UNIX AND NOT APPLE)
    target_link_libraries(isac_engine PUBLIC dl pthread m)
endif()

# 2. 主程序与 UI 层
set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    src/library/TrackMetadata.h
    src/library/TrackMetadata.cpp
    src/library/LibraryTypes.h
    src/library/LibraryRepository.h
    src/library/LibraryRepository.cpp
    src/library/LibraryScanner.h
    src/library/LibraryScanner.cpp
    src/engine/plugin/IAudioPlugin.h
    src/ui/apple_view/AppleMusicView.h
    src/ui/apple_view/AppleMusicView.cpp
    src/ui/player/AudioVisualization.h
    src/ui/player/AudioVisualization.cpp
    src/ui/patchbay/PatchbayView.h
    src/ui/patchbay/PatchbayView.cpp
    src/ui/patchbay/DspPresetCatalog.h
    src/ui/patchbay/DspPresetCatalog.cpp
    src/ui/inspector/NodeInspectorWidget.h
    src/ui/inspector/NodeInspectorWidget.cpp
    src/ui/plugins/PluginManager.h
    src/ui/plugins/PluginManager.cpp
    config/dsp-presets.qrc
)

qt_add_executable(ISACPlayer
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
)

target_include_directories(ISACPlayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ISACPlayer PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    PkgConfig::TAGLIB
    PkgConfig::FFTW3
    isac_engine
)

if(UNIX AND NOT APPLE)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS DBus)
    target_sources(ISACPlayer PRIVATE
        src/platform/mpris/MprisController.h
        src/platform/mpris/MprisController.cpp
    )
    target_link_libraries(ISACPlayer PRIVATE Qt${QT_VERSION_MAJOR}::DBus)
    target_compile_definitions(ISACPlayer PRIVATE ISAC_HAS_MPRIS=1)
endif()

add_subdirectory(plugins/replaygain)
add_subdirectory(plugins/gain)
add_subdirectory(plugins/adder)
add_subdirectory(plugins/biquad)

set_target_properties(ISACPlayer PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS ISACPlayer
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY DESTINATION ${CMAKE_INSTALL_BINDIR}/plugins)
install(DIRECTORY plugin_sdk/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/isacplayer
)

qt_finalize_executable(ISACPlayer)

add_custom_command(TARGET ISACPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:ISACPlayer>/plugins"
)

if(BUILD_TESTING)
    add_executable(isac_engine_tests
        tests/AudioEngineNodesTest.cpp
    )
    target_include_directories(isac_engine_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin_sdk/include
    )
    target_link_libraries(isac_engine_tests PRIVATE isac_engine)
    add_test(NAME isac_engine_tests COMMAND isac_engine_tests)

    add_executable(isac_patchbay_tests
        tests/PatchbaySceneTest.cpp
        src/ui/patchbay/PatchbayView.h
        src/ui/patchbay/PatchbayView.cpp
        src/ui/patchbay/DspPresetCatalog.h
        src/ui/patchbay/DspPresetCatalog.cpp
    )
    target_include_directories(isac_patchbay_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(isac_patchbay_tests PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        isac_engine
    )
    add_test(NAME isac_patchbay_tests COMMAND isac_patchbay_tests)
    set_tests_properties(isac_patchbay_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(isac_plugin_loader_tests
        tests/PluginLoaderTest.cpp
    )
    target_include_directories(isac_plugin_loader_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin_sdk/include
        ${CMAKE_CURRENT_SOURCE_DIR}/plugins/replaygain
    )
    target_link_libraries(isac_plugin_loader_tests PRIVATE isac_plugin_sdk)
    target_link_libraries(isac_plugin_loader_tests PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
    )
    add_test(NAME isac_plugin_loader_tests
        COMMAND isac_plugin_loader_tests
            $<TARGET_FILE:isac_replaygain>
            $<TARGET_FILE:isac_gain>
            $<TARGET_FILE:isac_adder>
            $<TARGET_FILE:isac_biquad>
    )

    add_executable(isac_library_tests
        tests/LibraryRepositoryTest.cpp
        src/library/LibraryRepository.h
        src/library/LibraryRepository.cpp
        src/library/LibraryScanner.h
        src/library/LibraryScanner.cpp
        src/library/TrackMetadata.h
        src/library/TrackMetadata.cpp
    )
    target_include_directories(isac_library_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(isac_library_tests PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Sql
        PkgConfig::TAGLIB
    )
    add_test(NAME isac_library_tests COMMAND isac_library_tests)
endif()
