cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME Wiegand_AR2106)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Collect mbedTLS 3.x library sources (populated before target_sources uses the variable)
file(GLOB MBEDTLS_LIBRARY_SOURCES
    "Drivers/mbedtls/library/*.c"
)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    Core/Src/main.c
    Core/Src/wiegand.c
    Core/Src/json_builder.c
    Core/Src/ethernet_manager.c
    Core/Src/tls_manager.c
    Core/Src/remote_server.c

    # WizNet ioLibrary driver sources
    Drivers/ioLibrary_Driver/Ethernet/socket.c
    Drivers/ioLibrary_Driver/Ethernet/wizchip_conf.c
    Core/Src/w5500_spi.c
    Drivers/ioLibrary_Driver/Ethernet/W5500/w5500.c
    Drivers/ioLibrary_Driver/Internet/DHCP/dhcp.c
    Drivers/ioLibrary_Driver/Internet/DNS/dns.c
    #Core/Src/HexTrans.c
    ${MBEDTLS_LIBRARY_SOURCES}
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    Core/Inc
    Drivers/CMSIS/Include
    Drivers/CMSIS/Device/ST/STM32F4xx/Include

    # WizNet ioLibrary headers
    Drivers/ioLibrary_Driver/Ethernet
    Drivers/ioLibrary_Driver/Ethernet/W5500
    Drivers/ioLibrary_Driver/Internet
    Drivers/ioLibrary_Driver/Internet/DHCP
    Drivers/ioLibrary_Driver/Internet/DNS
    Drivers/mbedtls/include
    Drivers/mbedtls/library
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Select the WIZnet chip in ioLibrary (defaults to W6300 otherwise)
    _WIZCHIP_=W5500
    # Point mbedTLS 3.6 at the project bare-metal config (Core/Inc is on the include path)
    MBEDTLS_CONFIG_FILE="mbedtls_user_config.h"
    
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
