cmake_minimum_required (VERSION 3.9)
project (OSPSuite.SimModelSolver_CVODES)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_FLAGS_DEBUG "-g")
set (CMAKE_CXX_FLAGS_RELEASE "-O3")

include_directories (
    ${OSPSuite.SimModelSolver_CVODES_SOURCE_DIR}/include 
	${OSPSuite.SimModelSolver_CVODES_SOURCE_DIR}/../CVODES/include 
	${OSPSuite.SimModelSolver_CVODES_SOURCE_DIR}/../CVODES/src/cvodes
	${OSPSuite.SimModelSolver_CVODES_SOURCE_DIR}/../OSPSuite.SimModelSolverBase/src/OSPSuite.SimModelSolverBase/include 
)

file (GLOB SOURCES 
        ${OSPSuite.SimModelSolver_CVODES_SOURCE_DIR}/src/*.cpp version.h
		${OSPSuite.SimModelSolver_CVODES_SOURCE_DIR}/../OSPSuite.SimModelSolverBase/src/OSPSuite.SimModelSolverBase/src/*.cpp
)

add_library (OSPSuite.SimModelSolver_CVODES SHARED ${SOURCES})

target_link_libraries (OSPSuite.SimModelSolver_CVODES ${OSPSuite.SimModelSolver_CVODES_SOURCE_DIR}/../../${libCVODES})