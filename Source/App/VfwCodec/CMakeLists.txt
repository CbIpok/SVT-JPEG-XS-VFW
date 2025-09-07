add_library(SvtJpegxsVfwCodec SHARED DriverProc.c)
target_include_directories(SvtJpegxsVfwCodec
    PRIVATE
        ${PROJECT_SOURCE_DIR}/Source/App/BufferCodec
        ${PROJECT_SOURCE_DIR}/Source/API)
target_link_libraries(SvtJpegxsVfwCodec
    PRIVATE
        SvtJpegxsBufferCodec
        SvtJpegxsLib)

install(TARGETS SvtJpegxsVfwCodec RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})

