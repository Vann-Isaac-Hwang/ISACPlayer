add_library(isac_stereo_width MODULE
    StereoWidthPlugin.cpp
    StereoWidthPlugin.h
    StereoWidthNode.h
)

target_include_directories(isac_stereo_width PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(isac_stereo_width PRIVATE
    isac_plugin_sdk
    Qt${QT_VERSION_MAJOR}::Widgets
)

set_target_properties(isac_stereo_width PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

if(WIN32)
    set_target_properties(isac_stereo_width PROPERTIES PREFIX "")
endif()

if(UNIX AND NOT APPLE)
    target_compile_options(isac_stereo_width PRIVATE -fvisibility=hidden)
endif()
