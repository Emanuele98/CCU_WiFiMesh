# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(BumblebeeWiFiMeshLite)

# After project() call, we can access CONFIG variables and set up version
if(DEFINED CONFIG_FW_VERSION_MAJOR)
    set(FW_VERSION_STRING "${CONFIG_FW_VERSION_MAJOR}.${CONFIG_FW_VERSION_MINOR}.${CONFIG_FW_VERSION_PATCH}")
    message(STATUS "Firmware Version: ${FW_VERSION_STRING}")
    
    # Add post-build commands to the main executable target
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME}.elf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "=== Firmware Version: ${FW_VERSION_STRING} ==="
        COMMENT "Displaying firmware version"
    )
    
    # Create versioned binary after the .bin file is generated
    add_custom_target(versioned_bin ALL
        COMMAND ${CMAKE_COMMAND} -E copy 
            ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin 
            ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}_v${FW_VERSION_STRING}.bin
        COMMENT "Creating versioned binary: ${CMAKE_PROJECT_NAME}_v${FW_VERSION_STRING}.bin"
        DEPENDS app
    )
endif()