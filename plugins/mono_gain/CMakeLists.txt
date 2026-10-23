add_library(isac_mono_gain MODULE
    MonoGainPlugin.cpp
    MonoGainPlugin.h
    MonoGainNode.h
)

target_include_directories(isac_mono_gain PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(isac_mono_gain PRIVATE
    isac_plugin_sdk
    Qt${QT_VERSION_MAJOR}::Widgets
)

set_target_properties(isac_mono_gain PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

if(WIN32)
    set_target_properties(isac_mono_gain PROPERTIES PREFIX "")
endif()

if(UNIX AND NOT APPLE)
    target_compile_options(isac_mono_gain PRIVATE -fvisibility=hidden)
endif()
