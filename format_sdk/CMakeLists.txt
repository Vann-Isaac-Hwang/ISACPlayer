add_library(isac_format_sdk INTERFACE)

target_include_directories(isac_format_sdk INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(isac_format_sdk INTERFACE
    Qt${QT_VERSION_MAJOR}::Core
)
