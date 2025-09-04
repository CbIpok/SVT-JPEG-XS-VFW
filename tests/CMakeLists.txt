add_subdirectory(UnitTests)

add_test(NAME encapp_buffer
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_encapp_buffer.sh
        $<TARGET_FILE:SvtJpegxsEncApp> $<TARGET_FILE:EncAppBuffer> ${CMAKE_CURRENT_SOURCE_DIR}/data)

add_test(NAME decapp_buffer
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_decapp_buffer.sh
        $<TARGET_FILE:SvtJpegxsDecApp> $<TARGET_FILE:DecAppBuffer>
        $<TARGET_FILE:SvtJpegxsEncApp> ${CMAKE_CURRENT_SOURCE_DIR}/data)
