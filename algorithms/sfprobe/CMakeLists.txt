project(sfprobe)

## Specify global include dirs and global libraries for this project
## such that, they can be used within other projects.
set(PACKAGE_sfprobe_INCLUDE_DIRS
    ${PACKAGE_sfprobe_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}
    CACHE INTERNAL "package include directories")
set(PACKAGE_sfprobe_LIBRARIES
    ${PACKAGE_sfprobe_LIBRARIES}
    sfprobe
    CACHE INTERNAL "package libraries")

set(SOURCES
    ExtentPartitioner.cxx
    ExtentUtilities.cxx
    LagrangianTesselator.cxx
    ParallelStructureFormationProbe.cxx
    SimpleMesh.cxx
    StructureFormationProbe.cxx
    TetrahedronUtilities.cxx
    VirtualGrid.cxx
    UniformProber.cxx
    )

if(ENABLE_DAX AND Dax_Cuda_FOUND)
    set(CUDA_SOURCES
        ExtentPartitioner.cxx
        ExtentUtilities.cxx
        LagrangianTesselator.cxx
        ParallelStructureFormationProbe.cxx
        SimpleMesh.cxx
        StructureFormationProbe.cxx
        TetrahedronUtilities.cxx
        VirtualGrid.cxx
        UniformProber.cu
        )
    cuda_add_library(cuda_sfprobe ${CUDA_SOURCES})
endif()

## Move header files to include directory in the build tree
file(GLOB HEADERS *.h)
foreach(header ${HEADERS})
  get_filename_component(headerFile ${header} NAME)
  file(COPY ${header} DESTINATION ${HEADER_INCLUDES_DIRECTORY})
endforeach()

## Specify global sources for this project
GetPackageSources(sfprobe ${PROJECT_SOURCE_DIR} "${SOURCES}")
if(BUILD_SHARED_LIBS)
 add_library(sfprobe SHARED ${SOURCES})
else()
 add_library(sfprobe STATIC ${SOURCES})
endif()

target_link_libraries(sfprobe ${MPI_C_LIBRARIES})

add_subdirectory(executables)
