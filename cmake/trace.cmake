# Define the __FILENAME__ to a relative, readable path
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D__FILENAME__='\"${CMAKE_PROJECT_NAME}/$(subst ${CMAKE_SOURCE_DIR}/,,$(abspath $<))\"'")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__FILENAME__='\"${CMAKE_PROJECT_NAME}/$(subst ${CMAKE_SOURCE_DIR}/,,$(abspath $<))\"'")
