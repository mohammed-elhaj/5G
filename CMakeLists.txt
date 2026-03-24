cmake_minimum_required(VERSION 3.14)
project(5g_layer2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header files
include_directories(${CMAKE_SOURCE_DIR}/include)

# Main executable
add_executable(5g_layer2
    src/main.cpp
    src/ip_generator.cpp
    src/pdcp.cpp
    src/rlc.cpp
    src/mac.cpp
)

# Unit tests — each links the layer sources it needs
add_executable(test_pdcp
    tests/test_pdcp.cpp
    src/pdcp.cpp
)

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

# ============================================================
# Custom targets for profiling and chart generation
# ============================================================

# Target: run_profiling
# Runs the profiling script to collect data with multiple configurations
if(WIN32)
    add_custom_target(run_profiling
        COMMAND ${CMAKE_COMMAND} -E echo "Running profiling suite..."
        COMMAND cmd /c ${CMAKE_SOURCE_DIR}/scripts/run_profiling.bat
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS 5g_layer2
        COMMENT "Running automated profiling with multiple TB and packet sizes"
    )
else()
    add_custom_target(run_profiling
        COMMAND ${CMAKE_COMMAND} -E echo "Running profiling suite..."
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/run_profiling.sh
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS 5g_layer2
        COMMENT "Running automated profiling with multiple TB and packet sizes"
    )
endif()

# Target: generate_charts
# Generates charts from profiling_results.csv
add_custom_target(generate_charts
    COMMAND ${CMAKE_COMMAND} -E echo "Generating profiling charts..."
    COMMAND python ${CMAKE_SOURCE_DIR}/scripts/generate_charts.py
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating charts from profiling data"
)

# Target: profile
# Complete profiling workflow: run profiling + generate charts
add_custom_target(profile
    COMMAND ${CMAKE_COMMAND} -E echo "=========================================="
    COMMAND ${CMAKE_COMMAND} -E echo "  Complete Profiling Workflow"
    COMMAND ${CMAKE_COMMAND} -E echo "=========================================="
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target run_profiling
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target generate_charts
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "Profiling complete! Charts saved to docs/charts/"
    DEPENDS 5g_layer2
    COMMENT "Run complete profiling suite and generate charts"
)

# Target: clean_profiling
# Remove profiling data and generated charts
add_custom_target(clean_profiling
    COMMAND ${CMAKE_COMMAND} -E remove -f ${CMAKE_BINARY_DIR}/profiling_results.csv
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_SOURCE_DIR}/docs/charts
    COMMAND ${CMAKE_COMMAND} -E echo "Profiling data and charts cleaned"
    COMMENT "Clean profiling results and charts"
)
