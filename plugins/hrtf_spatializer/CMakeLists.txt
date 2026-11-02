add_library(isac_hrtf_spatializer MODULE
    HrtfSpatializerPlugin.cpp
    HrtfSpatializerPlugin.h
    HrtfSpatializerNode.h
)

target_include_directories(isac_hrtf_spatializer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ISAC_MYSOFA_INCLUDE_DIR}
)

target_link_libraries(isac_hrtf_spatializer PRIVATE
    isac_plugin_sdk
    Qt${QT_VERSION_MAJOR}::Widgets
    ${ISAC_MYSOFA_LIBRARY}
)

set_target_properties(isac_hrtf_spatializer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

if(WIN32)
    set_target_properties(isac_hrtf_spatializer PROPERTIES PREFIX "")
endif()

if(UNIX AND NOT APPLE)
    target_compile_options(isac_hrtf_spatializer PRIVATE -fvisibility=hidden)
endif()
