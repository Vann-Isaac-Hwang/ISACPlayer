add_library(isac_adder MODULE
    AdderPlugin.cpp
    AdderPlugin.h
    AdderNode.h
)

target_include_directories(isac_adder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(isac_adder PRIVATE
    isac_plugin_sdk
    Qt${QT_VERSION_MAJOR}::Widgets
)

set_target_properties(isac_adder PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

if(WIN32)
    set_target_properties(isac_adder PROPERTIES PREFIX "")
endif()

if(UNIX AND NOT APPLE)
    target_compile_options(isac_adder PRIVATE -fvisibility=hidden)
endif()
