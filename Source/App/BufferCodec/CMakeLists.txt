add_library(SvtJpegxsBufferCodec STATIC BufferCodec.c)
target_include_directories(SvtJpegxsBufferCodec PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${PROJECT_SOURCE_DIR}/Source/API)
target_link_libraries(SvtJpegxsBufferCodec PUBLIC SvtJpegxsLib)
