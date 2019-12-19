add_executable(test_binary8 test_binary8.c)
target_link_libraries(test_binary8 nckernel_static)
target_include_directories(test_binary8 PRIVATE ${INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/../../src )
