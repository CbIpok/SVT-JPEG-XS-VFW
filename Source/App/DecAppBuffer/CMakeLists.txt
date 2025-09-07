#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

# APP Directory CMakeLists.txt

# Include Subdirectories
include_directories(
    ${PROJECT_SOURCE_DIR}/Source/API/
    ${PROJECT_SOURCE_DIR}/Source/App/Common/
    ${PROJECT_SOURCE_DIR}/Source/App/BufferCodec/
    )

# App Source Files (explicit to avoid old main)
add_executable(DecAppBuffer
    DecParamParser.c
    MainBufferCodec.c)

#********** SET COMPILE FLAGS************
# Link the Decoder App
target_link_libraries(DecAppBuffer
    SvtJpegxsBufferCodec SvtJpegxsLib)

if(UNIX)
    target_link_libraries(DecAppBuffer
        pthread
        m)
endif()

install(TARGETS DecAppBuffer RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})
