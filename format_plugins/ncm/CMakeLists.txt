add_library(isac_format_ncm MODULE
    NcmFormatPlugin.h
    NcmFormatPlugin.cpp
    NcmReader.h
    NcmReader.cpp
    third_party/aes.h
    third_party/aes.cpp
)

target_include_directories(isac_format_ncm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(isac_format_ncm PRIVATE
    isac_format_sdk
    Qt${QT_VERSION_MAJOR}::Core
)

set_target_properties(isac_format_ncm PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/format_plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/format_plugins"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/format_plugins"
)

include(GNUInstallDirs)
install(TARGETS isac_format_ncm
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}/format_plugins
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/format_plugins
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
