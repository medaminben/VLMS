# Google Test per library (Qt-free Core)

Approved 2026-09-05. UnitPro layout and CMake shape; Google Test instead of QTest.
Core and Ocr tests link no Qt. UI tests keep Qt Widgets.

## Layout

| Suite | Path | Executable |
|---|---|---|
| Core | `libraries/Core/test/` | `test_vlms_core` |
| Ocr | `libraries/Ocr/test/` | `test_vlms_ocr` |
| UI | `applications/vlms/test/` | `test_vlms_ui` |
| Shared fixtures | `libraries/Core/test/support/` | `vlms_testsupport` (no Qt) |
| SQL fixtures | `libraries/Core/test/data/` | — |
| OCR image | `libraries/Ocr/test/data/` | — |

## CMake

- `cmake/GTestSupport.cmake` — GoogleTest v1.15.2 via FetchContent
- `cmake/TestUtils.cmake` — `build_gtest_executable` (UnitPro naming + VLMS env/labels/timezones)
- `BUILD_TESTING` follows `VLMS_BUILD_TESTS`
- Top-level `tests/` and `create_test()` are removed
- One binary per library; timezone and realdb are extra CTest entries with `--gtest_filter`

## Conversion

- `TEST` / `TEST_F` / loops with `SCOPED_TRACE` (no QFETCH)
- Core helpers use `std::string`, `Date`, `SqlValue` — no `QString` / `QVariant` / `QTemporaryDir`
- UI `main` creates `QApplication`; Core/Ocr use `gtest_main`
