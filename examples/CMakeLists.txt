add_executable(ncrecv ncrecv.c)
set_target_properties(ncrecv PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(ncrecv nckernel_static)

add_executable(ncsend ncsend.c)
set_target_properties(ncsend PROPERTIES LINKER_LANGUAGE CXX )
target_link_libraries(ncsend nckernel_static)

add_executable(ncrelay ncrelay.c)
set_target_properties(ncrelay PROPERTIES LINKER_LANGUAGE CXX )
target_link_libraries(ncrelay nckernel_static)

add_executable(simulator simulator.c simulator_common.c)
set_target_properties(simulator PROPERTIES LINKER_LANGUAGE CXX )
target_link_libraries(simulator nckernel_static)

add_executable(segmenter segmenter.c)
set_target_properties(segmenter PROPERTIES LINKER_LANGUAGE CXX )
target_link_libraries(segmenter nckernel_static)

add_executable(simple_encode_recode_decode simple_encode_recode_decode.c)
set_target_properties(simple_encode_recode_decode PROPERTIES LINKER_LANGUAGE CXX )
target_link_libraries(simple_encode_recode_decode nckernel_static)

add_executable(coop_simulator coop_simulator.c)
set_target_properties(coop_simulator PROPERTIES LINKER_LANGUAGE CXX )
target_link_libraries(coop_simulator nckernel_static)
