cmake_minimum_required (VERSION 3.9)
project (OSPSuite.SimModelSolver_CVODES)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_FLAGS_DEBUG "-g")
set (CMAKE_CXX_FLAGS_RELEASE "-O3")

# RID is passed in by buildNix.sh (e.g. linux-x64, osx-arm64). The unified
# CVODES package ships cross-platform source headers under CVODES/include and
# platform-specific generated headers (sundials_config.h, sundials_export.h,
# sundials_fconfig.h) under runtimes/<rid>/native/include — both directories
# must be on the include path.
if (NOT DEFINED RID)
    message (FATAL_ERROR "RID must be passed to cmake (e.g. -DRID=linux-x64). buildNix.sh sets this from uname.")
endif ()

include_directories (
    ${OSPSuite.SimModelSolver_CVODES_SOURCE_DIR}/include
    ${OSPSuite.SimModelSolver_CVODES_SOURCE_DIR}/../../packages/CVODES/CVODES/include
    ${OSPSuite.SimModelSolver_CVODES_SOURCE_DIR}/../../packages/CVODES/runtimes/${RID}/native/include
    ${OSPSuite.SimModelSolver_CVODES_SOURCE_DIR}/../OSPSuite.SimModelSolverBase/src/OSPSuite.SimModelSolverBase/include
)

file (GLOB SOURCES 
        ${OSPSuite.SimModelSolver_CVODES_SOURCE_DIR}/src/*.cpp version.h
		${OSPSuite.SimModelSolver_CVODES_SOURCE_DIR}/../OSPSuite.SimModelSolverBase/src/OSPSuite.SimModelSolverBase/src/*.cpp
)

add_library (OSPSuite.SimModelSolver_CVODES SHARED ${SOURCES})

target_link_libraries (OSPSuite.SimModelSolver_CVODES ${OSPSuite.SimModelSolver_CVODES_SOURCE_DIR}/../../${libCVODES})