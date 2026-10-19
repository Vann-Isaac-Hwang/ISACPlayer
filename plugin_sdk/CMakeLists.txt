add_library(isac_plugin_sdk INTERFACE)

target_include_directories(isac_plugin_sdk INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_compile_features(isac_plugin_sdk INTERFACE cxx_std_20)
