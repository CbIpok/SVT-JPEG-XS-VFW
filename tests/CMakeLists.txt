add_subdirectory(UnitTests)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(NAME encapp_buffer
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_encapp_buffer.py
        $<TARGET_FILE:SvtJpegxsEncApp> $<TARGET_FILE:EncAppBuffer> ${CMAKE_CURRENT_SOURCE_DIR}/data)

add_test(NAME decapp_buffer
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_decapp_buffer.py
        $<TARGET_FILE:SvtJpegxsDecApp> $<TARGET_FILE:DecAppBuffer>
        $<TARGET_FILE:SvtJpegxsEncApp> ${CMAKE_CURRENT_SOURCE_DIR}/data)
