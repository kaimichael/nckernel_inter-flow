#.rst:
# FindKODOC
# ---------
#
# Find the kodoc libraries
#
# Imported Targets
# ^^^^^^^^^^^^^^^^
#
# If kodoc is found, this module defines the following :prop_tgt:`IMPORTED` targets::
#
#  KODOC::kodoc         - dynamic linked library
#  KODOC::kodoc_static  - statically linked library
#  KODOC::kodo_core     - statically linked kodo core
#  KODOC::kodo_rlnc     - statically linked kodo rlnc
#  KODOC::fifi          - static FIFI library
#  KODOC::cpuid         - static CPUID library
#
# Result Variables
# ^^^^^^^^^^^^^^^^
#
# This module will set the following variables::
#
#  KODOC_FOUND                  - True if kodoc was found.
#  KODOC_STATIC_FOUND           - True if kodoc_static was found.
#  KODOC_INCLUDE_DIRS           - Path to the header files.
#  KODOC_LIBRARIES              - Libraries required for kodoc.
#  KODOC_STATIC_LIBRARIES       - Static libraries for kodoc.
#
# Hints
# ^^^^^
#
# By default, kodoc is searched in the usual system paths, in the ``kodo-rlnc-c`` subdirectory of the
# build directory and at ``../kodo-rlnc-c`` relative to the source path. To search at a different
# location you can set the ``KODOC_HINT`` variable.
#
# Additionally, there are hints for all components. The name of the hint variable is KODOC_${NAME}_HINT,
# e.g. KODOC_FIFI_HINT or KODOC_KODO_CORE_HINT. The NAME is allways uppercase and and dashes replaced
# with underscores.

include(FindPackageHandleStandardArgs)

function(__kodoc_find_component _name _header)
    string(TOUPPER ${_name} _name_upper)
    string(REPLACE "-" "_" _var ${_name_upper} )
    set( _full_var KODOC_${_var}_INCLUDE_DIR )
    set( _hint KODOC_${_var}_HINT )
    find_path( ${_full_var}
        NAME ${_header}
        PATH_SUFFIXES ${_name}/src resolve_symlinks/${_name}/src
        PATHS ${_hint} ${KODOC_HINT} ${CMAKE_BUILD_DIR}/kodo-rlnc-c ${CMAKE_SOURCE_DIR}/../kodo-rlnc-c
        CMAKE_FIND_ROOT_PATH_BOTH
        )
    set( KODOC_INCLUDE_DIRS ${KODOC_INCLUDE_DIRS} ${${_full_var}} PARENT_SCOPE )
endfunction()

find_library( KODOC_STATIC_LIBRARY
    NAMES kodo_rlnc_c_static
    PATH_SUFFIXES build_current
    PATHS ${KODOC_HINT} ${CMAKE_SOURCE_DIR}/../kodo-rlnc-c ${CMAKE_BUILD_DIR}/kodo-rlnc-c
    )
find_library( KODOC_LIBRARY
    NAME kodo_rlnc_c
    PATH_SUFFIXES build_current
    PATHS ${KODOC_HINT} ${CMAKE_SOURCE_DIR}/../kodo-rlnc-c ${CMAKE_BUILD_DIR}/kodo-rlnc-c
    )
find_library( KODOC_CORE_LIBRARY
    NAME kodo_core
    PATH_SUFFIXES build_current/resolve_symlinks/kodo-core
    PATHS ${KODOC_HINT} ${CMAKE_SOURCE_DIR}/../kodo-rlnc-c ${CMAKE_BUILD_DIR}/kodo-rlnc-c
    )
find_library( KODOC_RLNC_LIBRARY
    NAME kodo_rlnc
    PATH_SUFFIXES build_current/resolve_symlinks/kodo-rlnc
    PATHS ${KODOC_HINT} ${CMAKE_SOURCE_DIR}/../kodo-rlnc-c ${CMAKE_BUILD_DIR}/kodo-rlnc-c
    )
find_library( KODOC_FIFI_LIBRARY
    NAME fifi
    PATH_SUFFIXES build_current/resolve_symlinks/fifi/src/fifi
    PATHS ${KODOC_HINT} ${CMAKE_SOURCE_DIR}/../kodo-rlnc-c ${CMAKE_BUILD_DIR}/kodo-rlnc-c
    )
find_library( KODOC_CPUID_LIBRARY
    NAME cpuid
    PATH_SUFFIXES build_current/resolve_symlinks/cpuid/src/cpuid
    PATHS ${KODOC_HINT} ${CMAKE_SOURCE_DIR}/../kodo-rlnc-c ${CMAKE_BUILD_DIR}/kodo-rlnc-c
    )

find_path( KODOC_INCLUDE_DIR
    NAME kodo_rlnc_c/encoder.h
    PATH_SUFFIXES kodo-rlnc-c/src src
    PATHS ${KODOC_HINT} ${CMAKE_BUILD_DIR}/kodo-rlnc-c ${CMAKE_SOURCE_DIR}/../kodo-rlnc-c
    CMAKE_FIND_ROOT_PATH_BOTH
    )
set( KODOC_INCLUDE_DIRS ${KODOC_INCLUDE_DIR} )

__kodoc_find_component( fifi        fifi/default_field.hpp )
__kodoc_find_component( cpuid       cpuid/cpuinfo.hpp )
#__kodoc_find_component( meta        meta/typelist.hpp )
__kodoc_find_component( hex         hex/dump.hpp )
__kodoc_find_component( kodo-core   kodo_core/payload_info.hpp )
#__kodoc_find_component( kodo-rlnc   kodo_rlnc/recoder_symbol_id.hpp )
__kodoc_find_component( platform    platform/config.hpp )
__kodoc_find_component( allocate    allocate/aligned_allocator.hpp )
__kodoc_find_component( storage     storage/storage.hpp )
#__kodoc_find_component( recycle     recycle/resource_pool.hpp )
__kodoc_find_component( endian      endian/big_endian.hpp )

find_path( KODOC_BOOST_INCLUDE_DIR
    NAME boost/dynamic_bitset.hpp
    PATH_SUFFIXES resolve_symlinks/boost
    PATHS ${KODOC_HINT} ${KODOC_BOOST_HINT} ${CMAKE_BUILD_DIR}/kodo-rlnc-c ${CMAKE_SOURCE_DIR}/../kodo-rlnc-c
    CMAKE_FIND_ROOT_PATH_BOTH
    )
set( KODOC_INCLUDE_DIRS ${KODOC_INCLUDE_DIRS} ${KODOC_BOOST_INCLUDE_DIR} )

set( KODOC_LIBRARIES ${KODOC_LIBRARY} )
set( KODOC_STATIC_LIBRARIES ${KODOC_STATIC_LIBRARY} ${KODOC_CORE_LIBRARY} ${KODOC_RLNC_LIBRARY} ${KODOC_FIFI_LIBRARY} ${KODOC_CPUID_LIBRARY} )

#find_package_handle_standard_args( KODOC
#    FOUND_VAR KODOC_FOUND
#    REQUIRED_VARS KODOC_INCLUDE_DIR KODOC_LIBRARY
#    )

find_package_handle_standard_args( KODOC_STATIC
    FOUND_VAR KODOC_STATIC_FOUND
    REQUIRED_VARS KODOC_INCLUDE_DIR KODOC_STATIC_LIBRARY KODOC_FIFI_LIBRARY KODOC_CPUID_LIBRARY
    )

if( KODOC_FOUND AND NOT TARGET KODOC::kodoc )
    add_library( KODOC::kodoc SHARED IMPORTED )
    set_target_properties( KODOC::kodoc PROPERTIES
        IMPORTED_LOCATION "${KODOC_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${KODOC_INCLUDE_DIRS}"
        IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
        )
endif()

if( KODOC_STATIC_FOUND AND NOT TARGET KODOC::kodoc_static )
    add_library( KODOC::fifi SHARED IMPORTED )
    set_target_properties( KODOC::fifi PROPERTIES
        IMPORTED_LOCATION "${KODOC_FIFI_LIBRARY}"
        IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
        )
    add_library( KODOC::cpuid SHARED IMPORTED )
    set_target_properties( KODOC::cpuid PROPERTIES
        IMPORTED_LOCATION "${KODOC_CPUID_LIBRARY}"
        IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
        )
    add_library( KODOC::kodo_core SHARED IMPORTED )
    set_target_properties( KODOC::kodo_core PROPERTIES
        IMPORTED_LOCATION "${KODOC_CORE_LIBRARY}"
        IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
        )
    add_library( KODOC::kodo_rlnc SHARED IMPORTED )
    set_target_properties( KODOC::kodo_rlnc PROPERTIES
        IMPORTED_LOCATION "${KODOC_RLNC_LIBRARY}"
        IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
        )
    add_library( KODOC::kodoc_static SHARED IMPORTED )
    set_property(TARGET KODOC::kodoc_static
        PROPERTY INTERFACE_LINK_LIBRARIES KODOC::kodo_rlnc KODOC::kodo_core KODOC::fifi KODOC::cpuid)
    set_target_properties( KODOC::kodoc_static PROPERTIES
        IMPORTED_LOCATION "${KODOC_STATIC_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${KODOC_INCLUDE_DIRS}"
        #INTERFACE_LINK_LIBRARIES "KODOC::fifi" "KODOC::cpuid"
        IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
        )
endif()
