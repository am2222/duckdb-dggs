cmake_minimum_required(VERSION 3.21)

# Set extension name here
set(TARGET_NAME duck_dggs)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME}
    VERSION 0.1.0
    LANGUAGES CXX C
    DESCRIPTION "DuckDB extension — DGGRID discrete global grid functions"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(src/include)

# --------------------------------------------------------------------------
# DGGRID submodule at third_party/DGGRID
# --------------------------------------------------------------------------
set(DGGRID_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/DGGRID")

if(NOT EXISTS "${DGGRID_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "DGGRID submodule not found at ${DGGRID_DIR}.\n"
        "Run: git submodule update --init --recursive")
endif()

option(WITH_GDAL "Build DGGRID with GDAL support" OFF)
set(DGGRID_SKIP_INSTALL ON CACHE BOOL "" FORCE)

add_subdirectory("${DGGRID_DIR}" "third_party/DGGRID" EXCLUDE_FROM_ALL)

set(DGLIB_INCLUDE_DIR "${DGGRID_DIR}/src/lib/dglib/include")

# --------------------------------------------------------------------------
# Extension sources
# --------------------------------------------------------------------------
set(EXTENSION_SOURCES
    src/duck_dggs_extension.cpp
    src/dggrid_transform.cpp
)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} "" ${EXTENSION_SOURCES})

target_include_directories(${EXTENSION_NAME} PRIVATE "${DGLIB_INCLUDE_DIR}")
target_include_directories(${LOADABLE_EXTENSION_NAME} PRIVATE "${DGLIB_INCLUDE_DIR}")

add_dependencies(${EXTENSION_NAME} dglib)
add_dependencies(${LOADABLE_EXTENSION_NAME} dglib)
target_link_libraries(${EXTENSION_NAME} $<TARGET_FILE:dglib>)
target_link_libraries(${LOADABLE_EXTENSION_NAME} $<TARGET_FILE:dglib>)

# MSVC does not expose M_PI / M_PI_2 etc. unless _USE_MATH_DEFINES is set
# before including <cmath>.  The DGGRID CMakeLists sets this for its own
# targets but not for external consumers of dglib headers, so we must add it
# ourselves for the duck_dggs compilation units.
if(MSVC)
    target_compile_definitions(${EXTENSION_NAME} PRIVATE _USE_MATH_DEFINES)
    target_compile_definitions(${LOADABLE_EXTENSION_NAME} PRIVATE _USE_MATH_DEFINES)
endif()

# On Linux, the GCC linker (including aarch64) treats duplicate COMDAT symbols
# (C++17 inline statics like LogicalType::DOUBLE) as hard errors when linking
# extension object files against libduckdb_static.a.  These symbols are
# identical across all TUs so multiple definitions are safe to merge.
# INTERFACE on the static extension propagates the flag to the final test
# binary that DuckDB's build system links (libduck_dggs_extension.a +
# libduckdb_static.a).  PRIVATE on the loadable extension covers the .so link.
if(UNIX AND NOT APPLE)
    target_link_options(${EXTENSION_NAME} INTERFACE -Wl,--allow-multiple-definition)
    target_link_options(${LOADABLE_EXTENSION_NAME} PRIVATE -Wl,--allow-multiple-definition)
endif()


install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
