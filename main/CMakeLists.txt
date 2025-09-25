idf_component_register(SRCS "main.c" "wifiMesh.c" "peer.c" "util.c"
                    INCLUDE_DIRS "." "include")

# Build version string from Kconfig
set(FW_VERSION_STRING "${CONFIG_FW_VERSION_MAJOR}.${CONFIG_FW_VERSION_MINOR}.${CONFIG_FW_VERSION_PATCH}")

# Print version during build
message(STATUS "Building Firmware Version: ${FW_VERSION_STRING}")
