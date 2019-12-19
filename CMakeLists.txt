cmake_minimum_required(VERSION 3.0)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# find the appropriate project version
include(git)
if(GIT_TAG MATCHES "v[0-9]+.[0-9]+-[0-9]+-[a-z0-9]+")
    string(REGEX REPLACE "v([0-9]+.[0-9]+)(-[0-9]+)?(-[a-z0-9]+)?" "\\1\\2" RAW_VERSION "${GIT_TAG}")
    string(REPLACE "-" "." SEMANTIC_VERSION "${RAW_VERSION}")
elseif(GIT_TAG MATCHES "v[0-9]+.[0-9]+")
    string(SUBSTRING "${GIT_TAG}" 1 -1 RAW_VERSION)
    set(SEMANTIC_VERSION "${RAW_VERSION}.0")
else()
    # if all fails we set a version that is before all other versions
    set(SEMANTIC_VERSION "0.0.0")
endif()

project(nckernel VERSION ${SEMANTIC_VERSION})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib")

include(trace)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -W -Wextra -pedantic -std=gnu11")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -W -Wextra -pedantic -std=c++14")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -funroll-loops -ftree-vectorize -finline-functions")

option(BUILD_SHARED "Build a shared nckernel library" ON)
option(BUILD_STATIC "Build a static nckernel library" ON)
option(BUILD_EXAMPLES "Build the example programs" OFF)
option(BUILD_TESTS "Build the test programs" OFF)
option(BUILD_DOCS "Build documentation" ON)
option(PACK_DEB "Build a debian package" ON)
option(PACK_RPM "Build a rpm package" OFF)

if (BUILD_TESTS AND NOT BUILD_EXAMPLES)
    message(WARNING "Force BUILD_EXAMPLES to ON for BUILD_TESTS")
    set(BUILD_EXAMPLES ON)
endif ()

option(ENABLE_LIBEVENT "Build with libevent support" OFF)

include(ExternalProject)
set_directory_properties(PROPERTIES EP_BASE ${CMAKE_BINARY_DIR}/dependencies)

option(WITH_KODO "Link with kodo libraries" OFF)

include(default_debug)
include(sanitize_debug)

install(FILES "${PROJECT_BINARY_DIR}/include/nckernel/config.h" DESTINATION include/nckernel)
include_directories("${PROJECT_BINARY_DIR}/include" "${CMAKE_SOURCE_DIR}/contrib")

set(SRCS
    src/nckernel.c src/config.c src/skb.c src/segment.c src/trace.c
    src/timer_base.c src/timer_schedule.c
    src/util/rate_dual.c src/util/rate_credit.c
    )
install(FILES
    include/nckernel/api.h include/nckernel/nckernel.h
    include/nckernel/segment.h include/nckernel/skb.h include/nckernel/timer.h
    DESTINATION include/nckernel
    )

option(ENABLE_NOCODE "Enable the nocode protocol" ON)
if(ENABLE_NOCODE)
    set(SRCS ${SRCS} src/nocode/config.c src/nocode/encoder.c src/nocode/decoder.c src/nocode/recoder.c)
    install(FILES include/nckernel/nocode.h DESTINATION include/nckernel)
endif()

option(ENABLE_NOACK "Enable the noack protocol" ON)
if(ENABLE_NOACK)
    set(WITH_KODO ON)
    set(SRCS ${SRCS} src/noack/config.c src/noack/encoder.c src/noack/decoder.c src/noack/recoder.c)
    install(FILES include/nckernel/noack.h DESTINATION include/nckernel)
endif()

# GACK is disabled by default because it is dangerous to use in practice
option(ENABLE_GACK "Enable the gack protocol" ON)
if(ENABLE_GACK)
    set(WITH_KODO ON)
    set(SRCS ${SRCS} src/gack/config.c src/gack/encoder.c src/gack/decoder.c src/gack/recoder.c)
    install(FILES include/nckernel/gack.h DESTINATION include/nckernel)
endif()

option(ENABLE_GSAW "Enable the gsaw protocol" ON)
if(ENABLE_GSAW)
    set(WITH_KODO ON)
    set(SRCS ${SRCS} src/gsaw/config.c src/gsaw/encoder.c src/gsaw/decoder.c)
    install(FILES include/nckernel/gsaw.h DESTINATION include/nckernel)
endif()

option(ENABLE_PACE "Enable the pace protocol" ON)
if(ENABLE_PACE)
    set(WITH_KODO ON)
    set(SRCS ${SRCS} src/pace/config.c src/pace/encoder.c src/pace/decoder.c src/pace/recoder.c src/util/helper.c)
    install(FILES include/nckernel/pace.h DESTINATION include/nckernel)
endif()

option(ENABLE_PACEMG "Enable the multigeneration pace protocol" ON)
if(ENABLE_PACEMG)
    set(WITH_KODO ON)
    set(SRCS ${SRCS} src/pacemg/config.c src/pacemg/encoder.c src/pacemg/decoder.c src/pacemg/recoder.c src/util/helper.c)
    install(FILES include/nckernel/pacemg.h DESTINATION include/nckernel)
endif()

option(ENABLE_TETRYS "Enable the tetrys protocol" ON)
if(ENABLE_TETRYS)
    set(WITH_KODO ON)
    set(SRCS ${SRCS} src/tetrys/config.c src/tetrys/encoder.c src/tetrys/decoder.c)
    install(FILES include/nckernel/tetrys.h DESTINATION include/nckernel)
endif()

option(ENABLE_CODARQ "Enable the coded ARQ protocol" ON)
if(ENABLE_CODARQ)
    set(WITH_KODO ON)
    set(SRCS ${SRCS} src/codarq/config.c src/codarq/encoder.c src/codarq/decoder.c src/util/helper.c)
    install(FILES include/nckernel/codarq.h DESTINATION include/nckernel)
endif()

option(ENABLE_INTERFLOW_SLIDING_WINDOW "Enable the interflow sliding_window protocol" ON)
if(ENABLE_INTERFLOW_SLIDING_WINDOW)
    set(WITH_KODO ON)
    set(SRCS ${SRCS} src/interflow_sw/config.c src/interflow_sw/encoder.cpp src/interflow_sw/decoder.cpp src/interflow_sw/recoder.cpp src/interflow_sw/common.cpp)
    install(FILES include/nckernel/interflowsw.h DESTINATION include/nckernel)
endif()

option(ENABLE_SLIDING_WINDOW "Enable the sliding_window protocol" ON)
if(ENABLE_SLIDING_WINDOW)
    set(WITH_KODO ON)
    set(SRCS ${SRCS} src/sliding_window/config.c src/sliding_window/encoder.cpp src/sliding_window/decoder.cpp src/sliding_window/recoder.cpp src/sliding_window/common.cpp)
    install(FILES include/nckernel/sw.h DESTINATION include/nckernel)
endif()

option(ENABLE_CHAIN "Enable chained protocols" ON)
if(ENABLE_CHAIN)
    set(SRCS ${SRCS} src/chain/config.c src/chain/encoder.c src/chain/decoder.c)
    install(FILES include/nckernel/chain.h DESTINATION include/nckernel)
endif()

if(WITH_KODO)
    find_package(KODOC REQUIRED)
    find_package(KodoSlidingWindow REQUIRED)
    set(INCLUDES ${KODO_INCLUDES})
    set(LIBS KODOC::kodoc_static KodoSlidingWindow)
    SET(SRCS ${SRCS} src/kodo.c src/util/finite_field.cpp)
endif()

configure_file(
    "${PROJECT_SOURCE_DIR}/include/nckernel/config.h.in"
    "${PROJECT_BINARY_DIR}/include/nckernel/config.h"
    )

if(ENABLE_LIBEVENT)
    set(LIBS ${LIBS} event)
    set(SRCS ${SRCS} src/timer_libevent.c)
endif()


if(BUILD_STATIC)
    add_library(nckernel_static STATIC ${SRCS})
    set_target_properties(nckernel_static PROPERTIES LINKER_LANGUAGE CXX)
    target_link_libraries(nckernel_static m)
    target_include_directories(nckernel_static PRIVATE ${INCLUDES} ${KODOC_INCLUDE_DIRS} ${KODO_SLIDING_WINDOW_INCLUDE_DIRS} PUBLIC include)

    if(WITH_KODO)
        file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/kodoc_objs)
        add_custom_command(TARGET nckernel_static
            POST_BUILD
            COMMAND ar x $<TARGET_FILE:KODOC::kodoc_static>
            COMMAND ar x $<TARGET_FILE:KODOC::fifi>
            COMMAND ar x $<TARGET_FILE:KODOC::cpuid>
            COMMAND ar x $<TARGET_FILE:KODOC::kodo_core>
            COMMAND ar x $<TARGET_FILE:KODOC::kodo_rlnc>
            COMMAND ar r $<TARGET_FILE:nckernel_static> *.o
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/kodoc_objs
            DEPENDS ${KODOC_STATIC_LIBRARIES}
            )
    endif()

    install(TARGETS nckernel_static DESTINATION lib)
endif()

if(BUILD_SHARED)
    add_library(nckernel SHARED ${SRCS})
    set_target_properties(nckernel PROPERTIES LINKER_LANGUAGE CXX)
    target_link_libraries(nckernel ${LIBS} -lm)
    target_include_directories(nckernel PRIVATE ${INCLUDES} PUBLIC include)
    install(TARGETS nckernel DESTINATION lib)
endif()

install(FILES ${LIB_KODOC_STATIC} ${LIB_FIFI} ${LIB_CPUID} DESTINATION lib)

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_DOCS)
    add_subdirectory(docs)
endif()

set(CPACK_GENERATOR TXZ)
if(PACK_DEB)
    set(CPACK_GENERATOR ${CPACK_GENERATOR} DEB)
endif()
if(PACK_RPM)
    set(CPACK_GENERATOR ${CPACK_GENERATOR} RPM)
endif()
set(CPACK_PACKAGE_NAME "nckernel")
set(CPACK_PACKAGE_CONTACT "Frank Gabriel <frank.gabriel@tu-dresden.de>")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION}+${GIT_HASH})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Library for network coded stream protocols")

include(CPack)
