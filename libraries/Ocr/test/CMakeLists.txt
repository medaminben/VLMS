build_gtest_executable(
    NAME test_vlms_ocr
    SRC src/test_ocr.cpp
    DEPENDS ${CMAKE_ROOT_NAME}::Ocr
    DISCOVER OFF
    LABELS core ocr
)
target_compile_definitions(test_vlms_ocr PRIVATE
    VLMS_TEST_OCR_DATA_DIR="${CMAKE_SOURCE_DIR}/libraries/Ocr/test/data"
)
