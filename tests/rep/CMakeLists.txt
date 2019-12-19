add_executable(test_rep test_code.c test_encoder.c test_decoder.c test_recoder.c)
target_link_libraries(test_rep nckernel_static)

add_executable(test_rep_sarq test_sarq.c)
target_link_libraries(test_rep_sarq)
target_include_directories(test_rep_sarq PRIVATE "${CMAKE_SOURCE_DIR}/src")
