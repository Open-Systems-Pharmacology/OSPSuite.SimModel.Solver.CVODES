cmake_minimum_required (VERSION 3.9)
project (OSPSuite.FuncParserNative)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_FLAGS_DEBUG "-g")
set (CMAKE_CXX_FLAGS_RELEASE "-O3")

include_directories (${OSPSuite.FuncParserNative_SOURCE_DIR}/include)
file (GLOB SOURCES ${OSPSuite.FuncParserNative_SOURCE_DIR}/src/*.cpp version.h)

add_library (OSPSuite.FuncParserNative SHARED ${SOURCES})
