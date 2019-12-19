# output directly into 'test' subdirectory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(test_encoder test_encoder.c)
target_link_libraries(test_encoder nckernel_static)
add_test(NAME test_encoder COMMAND test_encoder)

add_executable(test_decoder test_decoder.c)
target_link_libraries(test_decoder nckernel_static)
add_test(NAME test_decoder COMMAND test_decoder)

SET( ENV{CMAKE_BINARY_DIR}    ${CMAKE_BINARY_DIR})
add_test(NAME simulator COMMAND ${CMAKE_SOURCE_DIR}/tests/test_simulator.py)

if(ENABLE_TETRYS)
    add_subdirectory(tetrys)
endif()

if(ENABLE_REP)
    add_subdirectory(rep)
endif()
