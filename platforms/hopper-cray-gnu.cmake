## Configuration on Hopper using GNU compiler

## setup compilers
set(BASE_COMPILER_DIR /opt/cray/xt-asyncpe/default/bin)
set(CMAKE_C_COMPILER ${BASE_COMPILER_DIR}/cc)
set(CMAKE_CXX_COMPILER ${BASE_COMPILER_DIR}/CC)
set(CMAKE_FORTRAN_COMPILER ${BASE_COMPILER_DIR}/ftn)

set(GNUFLAGS "-O3 -ffast-math -funroll-loops")
set(CMAKE_CXX_FLAGS "${GNUFLAGS}")
set(CMAKE_C_FLAGS "${GNUFLAGS}")

set(CMAKE_FIND_ROOT_PATH
    /opt/cray/mpt/default/gni/mpich2-gnu/47)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
