cmake_minimum_required(VERSION 3.16)

project(ISACPlayer VERSION 0.1 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Sql)

# TagLib, FFTW3, and the optional libmysofa are resolved by pkg-config on
# Unix-like systems and by CMake package configs (vcpkg) on Windows.
include(cmake/IsacDependencies.cmake)

if(WIN32)
    # The executable is a GUI application with a Windows resource block.
    enable_language(RC)
    # Keep windows.h from defining the min/max macros, which conflict with
    # std::min/std::max, and from pulling in the rarely used Win32 surface.
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(MSVC)
    # Source files contain UTF-8 (comments and the note glyph in the default
    # cover art). Without /utf-8 MSVC reads them with the system code page.
    add_compile_options(/utf-8)
endif()

# Platform libraries used by the theme manager's native window integration.
add_library(isac_theme_platform INTERFACE)
if(WIN32)
    # Dark title bars are a DWM attribute on Windows.
    target_link_libraries(isac_theme_platform INTERFACE dwmapi)
endif()

# Public headers consumed by external DSP plugins. This subproject intentionally
# carries no Qt or engine implementation dependency.
add_subdirectory(plugin_sdk)
add_subdirectory(format_sdk)

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
    src/media/formats/FormatCache.h
    src/media/formats/FormatCache.cpp
    src/media/formats/FormatPluginManager.h
    src/media/formats/FormatPluginManager.cpp
    src/media/formats/FormatPlaybackPrefetcher.h
    src/media/formats/FormatPlaybackPrefetcher.cpp
    src/platform/PluginPaths.h
    src/platform/PluginPaths.cpp
    src/engine/plugin/IAudioPlugin.h
    src/ui/isac_music/ISACMusicView.h
    src/ui/isac_music/ISACMusicView.cpp
    src/ui/isac_music/PlaybackQueueView.h
    src/ui/isac_music/PlaybackQueueView.cpp
    src/ui/widgets/HoverIconButton.h
    src/ui/widgets/HoverIconButton.cpp
    src/ui/widgets/HoverSlider.h
    src/ui/widgets/HoverSlider.cpp
    src/ui/widgets/SmoothScrollFilter.h
    src/ui/widgets/SmoothScrollFilter.cpp
    src/ui/player/AudioVisualization.h
    src/ui/player/AudioVisualization.cpp
    src/ui/patchbay/PatchbayView.h
    src/ui/patchbay/PatchbayView.cpp
    src/ui/patchbay/DspPresetCatalog.h
    src/ui/patchbay/DspPresetCatalog.cpp
    src/ui/inspector/NodeInspectorWidget.h
    src/ui/inspector/NodeInspectorWidget.cpp
    src/ui/theme/ThemeManager.h
    src/ui/theme/ThemeManager.cpp
    src/ui/plugins/PluginManager.h
    src/ui/plugins/PluginManager.cpp
    config/app-icons.qrc
    config/dsp-presets.qrc
)

qt_add_executable(ISACPlayer
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
)

target_include_directories(ISACPlayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(ISACPlayer PRIVATE
    ISACPLAYER_INSTALL_LIBDIR=\"${CMAKE_INSTALL_LIBDIR}\"
)

target_link_libraries(ISACPlayer PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    isac_taglib
    isac_fftw3
    isac_engine
    isac_format_sdk
    isac_theme_platform
)

if(WIN32)
    # Compile the application icon and version block into the executable. The
    # resource file is generated so it can reference an absolute icon path.
    set(ISAC_APP_ICON_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/isacplayer.ico")
    set(ISAC_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
    set(ISAC_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
    set(ISAC_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
    if(ISAC_VERSION_PATCH STREQUAL "")
        set(ISAC_VERSION_PATCH "0")
    endif()
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/isacplayer.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/isacplayer.rc"
        @ONLY)
    target_sources(ISACPlayer PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/isacplayer.rc")
endif()

if(UNIX AND NOT APPLE)
    if(QT_VERSION_MAJOR EQUAL 6)
        pkg_check_modules(KWINDOWSYSTEM QUIET IMPORTED_TARGET KF6WindowSystem)
    else()
        pkg_check_modules(KWINDOWSYSTEM QUIET IMPORTED_TARGET KF5WindowSystem)
    endif()

    if(TARGET PkgConfig::KWINDOWSYSTEM)
        target_link_libraries(ISACPlayer PRIVATE PkgConfig::KWINDOWSYSTEM)
        target_compile_definitions(ISACPlayer PRIVATE ISAC_HAS_KWINDOWSYSTEM=1)
    endif()
endif()

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
add_subdirectory(plugins/mono_gain)
add_subdirectory(plugins/mono_delay)
add_subdirectory(plugins/mono_feedback_delay)
add_subdirectory(plugins/stereo_width)
add_subdirectory(plugins/mono_modulated_delay)
add_subdirectory(plugins/stereo_balance)
add_subdirectory(plugins/adder)
add_subdirectory(plugins/biquad)
add_subdirectory(format_plugins/ncm)
if(TARGET isac_mysofa)
    add_subdirectory(plugins/hrtf_spatializer)
endif()

set(ISAC_DSP_PLUGIN_TARGETS
    isac_replaygain
    isac_gain
    isac_mono_gain
    isac_mono_delay
    isac_mono_feedback_delay
    isac_stereo_width
    isac_mono_modulated_delay
    isac_stereo_balance
    isac_adder
    isac_biquad
)
if(TARGET isac_hrtf_spatializer)
    list(APPEND ISAC_DSP_PLUGIN_TARGETS isac_hrtf_spatializer)
endif()

# Plugin modules are discovered from the directory beside the executable, so
# their output location must not depend on the generator or the configuration.
# Multi-configuration generators would otherwise emit plugins/Debug/*.dll while
# the executable looks in plugins/.
function(isac_pin_module_output_directory target directory)
    set_target_properties(${target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${directory}"
        RUNTIME_OUTPUT_DIRECTORY "${directory}"
        # Import libraries produced by Windows module targets are build-only
        # artifacts and must not be mistaken for loadable modules.
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )
    foreach(configuration IN LISTS CMAKE_CONFIGURATION_TYPES)
        string(TOUPPER "${configuration}" upper)
        set_target_properties(${target} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY_${upper} "${directory}"
            RUNTIME_OUTPUT_DIRECTORY_${upper} "${directory}"
            ARCHIVE_OUTPUT_DIRECTORY_${upper} "${CMAKE_BINARY_DIR}/lib"
        )
    endforeach()
endfunction()

foreach(plugin_target IN LISTS ISAC_DSP_PLUGIN_TARGETS)
    isac_pin_module_output_directory(
        ${plugin_target} "${CMAKE_BINARY_DIR}/plugins")
endforeach()
isac_pin_module_output_directory(
    isac_format_ncm "${CMAKE_BINARY_DIR}/format_plugins")

set_target_properties(ISACPlayer PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

if(WIN32)
    # Windows deployment is a flat directory: ISACPlayer.exe, its Qt runtime,
    # and the plugin module directories beside it.
    set(ISAC_INSTALL_DOCDIR "licenses")
    install(TARGETS ISACPlayer
        RUNTIME DESTINATION .
    )
    install(TARGETS ${ISAC_DSP_PLUGIN_TARGETS}
        RUNTIME DESTINATION plugins
        LIBRARY DESTINATION plugins
    )
else()
    set(ISAC_INSTALL_DOCDIR "${CMAKE_INSTALL_DATADIR}/licenses/isacplayer")
    install(TARGETS ISACPlayer
        BUNDLE DESTINATION .
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(TARGETS ${ISAC_DSP_PLUGIN_TARGETS}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/isacplayer/plugins
        RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/isacplayer/plugins
    )
endif()
if(UNIX AND NOT APPLE)
    install(FILES config/isacplayer.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES resources/icons/isacplayer.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps
    )
endif()
install(FILES LICENSE
    DESTINATION ${ISAC_INSTALL_DOCDIR}
)
install(FILES format_plugins/ncm/THIRD_PARTY_LICENSE.txt
    DESTINATION ${ISAC_INSTALL_DOCDIR}
    RENAME ncm-aes-MIT.txt
)
install(FILES third_party/miniaudio/LICENSE.txt
    DESTINATION ${ISAC_INSTALL_DOCDIR}
)
install(DIRECTORY plugin_sdk/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/isacplayer
)
install(DIRECTORY format_sdk/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/isacplayer
)

if(WIN32)
    # Lets `cmake --install` copy the Qt runtime next to the executable. The
    # deployment layout is flat, so the Qt deploy script must be told to use
    # the install root instead of CMAKE_INSTALL_BINDIR. The variables are set
    # in the generated install script because Qt bakes its defaults into the
    # deploy support module at configure time.
    qt_generate_deploy_app_script(
        TARGET ISACPlayer
        OUTPUT_SCRIPT ISAC_DEPLOY_SCRIPT
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(CODE "set(QT_DEPLOY_BIN_DIR \".\")")
    install(CODE "set(QT_DEPLOY_PLUGIN_DIR \"plugins\")")
    install(SCRIPT ${ISAC_DEPLOY_SCRIPT})
endif()

qt_finalize_executable(ISACPlayer)

add_custom_command(TARGET ISACPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:ISACPlayer>/plugins"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:ISACPlayer>/format_plugins"
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
        src/ui/theme/ThemeManager.h
        src/ui/theme/ThemeManager.cpp
    )
    target_include_directories(isac_patchbay_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(isac_patchbay_tests PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        isac_engine
        isac_theme_platform
    )
    add_test(NAME isac_patchbay_tests COMMAND isac_patchbay_tests)
    set_tests_properties(isac_patchbay_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(isac_theme_tests
        tests/ThemeTest.cpp
        src/ui/theme/ThemeManager.h
        src/ui/theme/ThemeManager.cpp
    )
    target_include_directories(isac_theme_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(isac_theme_tests PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        isac_theme_platform
    )
    add_test(NAME isac_theme_tests COMMAND isac_theme_tests)
    set_tests_properties(isac_theme_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(isac_smooth_scroll_tests
        tests/SmoothScrollFilterTest.cpp
        src/ui/widgets/SmoothScrollFilter.h
        src/ui/widgets/SmoothScrollFilter.cpp
    )
    target_include_directories(isac_smooth_scroll_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(isac_smooth_scroll_tests PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
    )
    add_test(NAME isac_smooth_scroll_tests COMMAND isac_smooth_scroll_tests)
    set_tests_properties(isac_smooth_scroll_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(isac_plugin_loader_tests
        tests/PluginLoaderTest.cpp
    )
    target_include_directories(isac_plugin_loader_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin_sdk/include
        ${CMAKE_CURRENT_SOURCE_DIR}/plugins/replaygain
        ${CMAKE_CURRENT_SOURCE_DIR}/plugins/biquad
    )
    target_link_libraries(isac_plugin_loader_tests PRIVATE isac_plugin_sdk)
    target_link_libraries(isac_plugin_loader_tests PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
    )
    set(ISAC_PLUGIN_TEST_TARGETS
        $<TARGET_FILE:isac_replaygain>
        $<TARGET_FILE:isac_gain>
        $<TARGET_FILE:isac_mono_gain>
        $<TARGET_FILE:isac_mono_delay>
        $<TARGET_FILE:isac_mono_feedback_delay>
        $<TARGET_FILE:isac_stereo_width>
        $<TARGET_FILE:isac_mono_modulated_delay>
        $<TARGET_FILE:isac_stereo_balance>
        $<TARGET_FILE:isac_adder>
        $<TARGET_FILE:isac_biquad>
    )
    if(TARGET isac_hrtf_spatializer)
        target_include_directories(isac_plugin_loader_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/plugins/hrtf_spatializer
            ${ISAC_MYSOFA_INCLUDE_DIR}
        )
        target_link_libraries(isac_plugin_loader_tests PRIVATE
            ${ISAC_MYSOFA_LIBRARY}
        )
        target_compile_definitions(isac_plugin_loader_tests PRIVATE
            ISAC_HAS_MYSOFA=1
        )
        if(EXISTS "/usr/share/libmysofa/default.sofa")
            target_compile_definitions(isac_plugin_loader_tests PRIVATE
                ISAC_TEST_SOFA_PATH="/usr/share/libmysofa/default.sofa"
            )
        else()
            find_file(ISAC_TEST_SOFA_PATH_FILE NAMES default.sofa
                PATHS "${ISAC_MYSOFA_INCLUDE_DIR}/../share/libmysofa"
                      "${ISAC_MYSOFA_INCLUDE_DIR}/../share/mysofa")
            if(ISAC_TEST_SOFA_PATH_FILE)
                target_compile_definitions(isac_plugin_loader_tests PRIVATE
                    ISAC_TEST_SOFA_PATH="${ISAC_TEST_SOFA_PATH_FILE}"
                )
            endif()
        endif()
        list(APPEND ISAC_PLUGIN_TEST_TARGETS
            $<TARGET_FILE:isac_hrtf_spatializer>)
    endif()
    add_test(NAME isac_plugin_loader_tests
        COMMAND isac_plugin_loader_tests
            ${ISAC_PLUGIN_TEST_TARGETS}
    )

    add_executable(isac_plugin_paths_tests
        tests/PluginPathsTest.cpp
        src/platform/PluginPaths.h
        src/platform/PluginPaths.cpp
    )
    target_include_directories(isac_plugin_paths_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(isac_plugin_paths_tests PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
    )
    add_test(NAME isac_plugin_paths_tests COMMAND isac_plugin_paths_tests)

    add_executable(isac_library_tests
        tests/LibraryRepositoryTest.cpp
        src/library/LibraryRepository.h
        src/library/LibraryRepository.cpp
        src/library/LibraryScanner.h
        src/library/LibraryScanner.cpp
        src/library/TrackMetadata.h
        src/library/TrackMetadata.cpp
        src/media/formats/FormatCache.h
        src/media/formats/FormatCache.cpp
        src/media/formats/FormatPluginManager.h
        src/media/formats/FormatPluginManager.cpp
    )
    target_include_directories(isac_library_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(isac_library_tests PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Sql
        isac_taglib
        isac_format_sdk
    )
    add_test(NAME isac_library_tests COMMAND isac_library_tests)
endif()
