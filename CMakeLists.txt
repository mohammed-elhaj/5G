cmake_minimum_required(VERSION 3.14)
project(5g_layer2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header files
include_directories(${CMAKE_SOURCE_DIR}/include)

# OpenSSL for PDCP security algorithms (AES-128-CTR, HMAC-SHA256)
find_package(OpenSSL REQUIRED)

# Main executable
add_executable(5g_layer2
    src/main.cpp
    src/ip_generator.cpp
    src/pdcp.cpp
    src/rlc.cpp
    src/mac.cpp
)
target_link_libraries(5g_layer2 OpenSSL::SSL OpenSSL::Crypto)

# Unit tests — each links the layer sources it needs
add_executable(test_pdcp
    tests/test_pdcp.cpp
    src/pdcp.cpp
)
target_link_libraries(test_pdcp OpenSSL::SSL OpenSSL::Crypto)

add_executable(test_rlc
    tests/test_rlc.cpp
    src/rlc.cpp
)

add_executable(test_mac
    tests/test_mac.cpp
    src/mac.cpp
)

add_executable(test_integration
    tests/test_integration.cpp
    src/ip_generator.cpp
    src/pdcp.cpp
    src/rlc.cpp
    src/mac.cpp
)
target_link_libraries(test_integration OpenSSL::SSL OpenSSL::Crypto)
