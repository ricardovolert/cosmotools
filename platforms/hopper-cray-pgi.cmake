## Configuration on Hopper using PGI compilers (default)

## setup compilers
set(BASE_COMPILER_DIR /opt/cray/xt-asyncpe/5.12/bin)
set(CMAKE_C_COMPILER ${BASE_COMPILER_DIR}/cc)
set(CMAKE_CXX_COMPILER ${BASE_COMPILER_DIR}/CC)
set(CMAKE_FORTRAN_COMPILER ${BASE_COMPILER_DIR}/ftn)

set(CMAKE_FIND_ROOT_PATH
    /opt/cray/mpt/5.5.2/gni/mpich2-pgi/119)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
