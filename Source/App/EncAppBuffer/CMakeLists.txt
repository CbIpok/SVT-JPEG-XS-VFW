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
add_executable(EncAppBuffer
    EncAppConfig.c
    MainBufferCodec.c)

#********** SET COMPILE FLAGS************
# Link the Encoder App
target_link_libraries(EncAppBuffer
    SvtJpegxsLib
    vfw32
    avifil32)

if(UNIX)
    target_link_libraries(EncAppBuffer
        pthread
        m)
endif()


install(TARGETS EncAppBuffer RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})
