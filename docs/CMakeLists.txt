find_program(SPHINX_BUILD sphinx-build DOC "sphinx-build binary")
add_custom_target(
    html
    COMMAND ${SPHINX_BUILD} -M html ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
    )
