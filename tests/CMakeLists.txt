cmake_minimum_required(VERSION 3.20)

# Tests executable
add_executable(pdfmark_tests
    TestMain.cpp
    WatermarkTileLayoutTest.cpp
    WatermarkVisualTest.cpp
    WatermarkConfigTest.cpp
    MemoryModelTest.cpp
    CommonTest.cpp
    MultiWatermarkBatchTest.cpp
)

target_link_libraries(pdfmark_tests PRIVATE
    pdfmark_core
    Qt6::Test
)

# Watermark layout test
add_test(NAME WatermarkTileLayoutTest COMMAND pdfmark_tests --test TileLayout)

# Visual output test
add_test(NAME WatermarkVisualTest COMMAND pdfmark_tests --test WatermarkVisual)

# Config validation test
add_test(NAME WatermarkConfigTest COMMAND pdfmark_tests --test WatermarkConfig)

# Memory model test
add_test(NAME MemoryModelTest COMMAND pdfmark_tests --test MemoryModel)

if(APPLE)
    set_target_properties(pdfmark_tests PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@loader_path/../../;@loader_path/."
    )
    add_custom_command(TARGET pdfmark_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:pdfium>
            $<TARGET_FILE_DIR:pdfmark_tests>
    )
elseif(WIN32)
    add_custom_command(TARGET pdfmark_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PDFium_LIBRARY}"
            $<TARGET_FILE_DIR:pdfmark_tests>
    )
endif()
# Common types test
add_test(NAME CommonTest COMMAND pdfmark_tests --test Common)
# Multi-watermark batch test
add_test(NAME MultiWatermarkBatchTest COMMAND pdfmark_tests --test MultiWatermarkBatch)
