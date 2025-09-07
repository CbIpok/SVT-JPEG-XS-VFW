add_executable(BufferAppTests integration/BufferAppTests.cpp)

# Require C++17 for filesystem
set_target_properties(BufferAppTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

# Link against GoogleTest and ensure apps are built first
add_dependencies(BufferAppTests SvtJpegxsEncApp EncAppBuffer SvtJpegxsDecApp DecAppBuffer)
target_link_libraries(BufferAppTests PRIVATE gtest gtest_main)

target_compile_definitions(BufferAppTests PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

# Runner that enforces using override data and validates outputs
add_executable(BufferAppsRunner integration/BufferAppsRunner.cpp)
set_target_properties(BufferAppsRunner PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_dependencies(BufferAppsRunner SvtJpegxsEncApp EncAppBuffer SvtJpegxsDecApp DecAppBuffer)
target_compile_definitions(BufferAppsRunner PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

add_test(NAME buffer_apps
    COMMAND BufferAppsRunner
    WORKING_DIRECTORY $<TARGET_FILE_DIR:BufferAppsRunner>)
