#.rst:
# FindKodoSlidingWindow
# ---------
#
# Find the kodo-sliding-window library
#
# Imported Targets
# ^^^^^^^^^^^^^^^^
#
# If kodo-sliding-window is found, this module defines the following :prop_tgt:`IMPORTED` targets::
#
#  KodoSlidingWindow
#
# Result Variables
# ^^^^^^^^^^^^^^^^
#
# This module will set the following variables::
#
#  KODO_SLIDING_WINDOW_FOUND        - True if kodo-sliding-window was found.
#  KODO_SLIDING_WINDOW_INCLUDE_DIRS - Path to the header files.
#
# Hints
# ^^^^^
#
# By default, kodoc is searched in the usual system paths, in the
# ``kodo-sliding-window`` subdirectory of the build directory and at
# ``../kodo-sliding-window`` relative to the source path. To search at a
# different location you can set the ``KODO_SLIDING_WINDOW_HINT`` variable.

include(FindPackageHandleStandardArgs)

find_path( KODO_SLIDING_WINDOW_INCLUDE_DIR
    NAME kodo_sliding_window/sliding_window_encoder.hpp
    PATH_SUFFIXES kodo-sliding-window/src
    PATHS ${KODO_SLIDING_WINDOW_HINT} ${CMAKE_BUILD_DIR} ${CMAKE_SOURCE_DIR}/..
    CMAKE_FIND_ROOT_PATH_BOTH
    )

set( KODO_SLIDING_WINDOW_INCLUDE_DIRS ${KODO_SLIDING_WINDOW_INCLUDE_DIR} )

find_package_handle_standard_args( KODO_SLIDING_WINDOW
    FOUND_VAR KODO_SLIDING_WINDOW_FOUND
    REQUIRED_VARS KODO_SLIDING_WINDOW_INCLUDE_DIR
    )

if( KODO_SLIDING_WINDOW_FOUND )
    add_library( KodoSlidingWindow INTERFACE IMPORTED )
    set_target_properties( KodoSlidingWindow PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${KODO_SLIDING_WINDOW_INCLUDE_DIRS}"
        )
endif()
