
set(component_srcs
    "src/esp32_azure_client.cpp"
)

set(component_includedirs
    "src"
)

idf_component_register(SRCS ${component_srcs}
    INCLUDE_DIRS  ${component_includedirs}
    REQUIRES "esp-azure"
)
