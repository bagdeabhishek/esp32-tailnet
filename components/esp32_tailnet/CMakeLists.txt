idf_component_register(
    SRCS "esp32_tailnet.cpp"
    INCLUDE_DIRS "include"
    REQUIRES microlink
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
