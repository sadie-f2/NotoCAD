add_executable(noto_tests
  test_main.cpp
  test_math.cpp
  test_entities.cpp
  test_dxf.cpp
  test_lisp.cpp
)

target_include_directories(noto_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(noto_tests PRIVATE noto_core noto_lisp noto_flags)

add_test(NAME noto_tests COMMAND noto_tests)
