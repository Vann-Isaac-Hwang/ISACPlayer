add_library(isac_gain MODULE
    GainPlugin.cpp
    GainPlugin.h
    GainNode.h
)

target_include_directories(isac_gain PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(isac_gain PRIVATE
    isac_plugin_sdk
    Qt${QT_VERSION_MAJOR}::Widgets
)

set_target_properties(isac_gain PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

if(WIN32)
    set_target_properties(isac_gain PROPERTIES PREFIX "")
endif()

if(UNIX AND NOT APPLE)
    target_compile_options(isac_gain PRIVATE -fvisibility=hidden)
endif()
