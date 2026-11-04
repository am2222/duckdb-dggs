cmake_minimum_required(VERSION 3.21)

# Set extension name here
set(TARGET_NAME duck_dggs)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(
  ${TARGET_NAME}
  VERSION 0.2.0 # x-release-please-version
  LANGUAGES CXX C
  DESCRIPTION "DuckDB extension — DGGRID discrete global grid functions")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(src/include)

# --------------------------------------------------------------------------
# DGGRID submodule at third_party/DGGRID
# --------------------------------------------------------------------------
set(DGGRID_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/DGGRID")

if(NOT EXISTS "${DGGRID_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "DGGRID submodule not found at ${DGGRID_DIR}.\n"
                      "Run: git submodule update --init --recursive")
endif()

option(WITH_GDAL "Build DGGRID with GDAL support" OFF)
# DGGRID v9.1 links an installed H3 library when it finds one; keep the build
# self-contained (and identical on every machine) by never looking for it.
option(WITH_H3 "Build DGGRID with H3 support" OFF)
set(DGGRID_SKIP_INSTALL
    ON
    CACHE BOOL "" FORCE)

# On Windows, M_PI is not defined by default — not by MSVC and not by MinGW/GCC
# either (MinGW skips the if(MSVC) block in DGGRID's CMakeLists.txt).
# add_compile_definitions here is directory-scoped: it propagates into the
# DGGRID add_subdirectory below, so dglib itself also gets the define.
if(WIN32)
  add_compile_definitions(_USE_MATH_DEFINES)
endif()

add_subdirectory("${DGGRID_DIR}" "third_party/DGGRID" EXCLUDE_FROM_ALL)

# dglib's report(..., DgBase::Fatal) prints to stderr and calls exit(1), which
# kills the whole DuckDB process (CLI, Python kernel, server) on any input dglib
# cannot handle. Build dglib from a copy of DgBase.cpp in which Fatal throws
# instead, so the error surfaces as an ordinary query error. Every Fatal goes
# through this one function, so this covers the whole library.
set(DGBASE_SRC "${DGGRID_DIR}/src/lib/dglib/lib/DgBase.cpp")
set(DGBASE_PATCHED "${CMAKE_CURRENT_BINARY_DIR}/dglib_patched/DgBase.cpp")
file(READ "${DGBASE_SRC}" _dgbase)
set(_fatal_old
    "         dgcerr << \"FATAL ERROR: \" << message << std::endl;\n#ifndef DGGRIDR\n         exit(1);\n#endif"
)
set(_fatal_new "         throw std::runtime_error(\"DGGRID: \" + message);")
string(FIND "${_dgbase}" "${_fatal_old}" _fatal_pos)
if(_fatal_pos EQUAL -1)
  message(
    FATAL_ERROR
      "DgBase.cpp no longer has the expected Fatal/exit(1) code; update the "
      "patch in CMakeLists.txt (it keeps dglib errors from exiting DuckDB).")
endif()
string(REPLACE "${_fatal_old}" "${_fatal_new}" _dgbase "${_dgbase}")
# Written via a temporary file so the copy (and dglib) only rebuilds when the
# content changes.
file(
  WRITE "${DGBASE_PATCHED}.tmp"
  "// Generated from ${DGBASE_SRC} by duck_dggs's CMakeLists.txt\n#include <stdexcept>\n${_dgbase}"
)
configure_file("${DGBASE_PATCHED}.tmp" "${DGBASE_PATCHED}" COPYONLY)
set_property(
  DIRECTORY
  APPEND
  PROPERTY CMAKE_CONFIGURE_DEPENDS "${DGBASE_SRC}")
get_target_property(_dglib_srcs dglib SOURCES)
list(TRANSFORM _dglib_srcs REPLACE "^(.*/)?lib/DgBase\\.cpp$"
                                   "${DGBASE_PATCHED}")
if(NOT "${DGBASE_PATCHED}" IN_LIST _dglib_srcs)
  message(FATAL_ERROR "could not find lib/DgBase.cpp in the dglib sources")
endif()
set_target_properties(dglib PROPERTIES SOURCES "${_dglib_srcs}")

set(DGLIB_INCLUDE_DIR "${DGGRID_DIR}/src/lib/dglib/include")

# --------------------------------------------------------------------------
# Z7 core library + authalic latitude conversion (submodule at
# third_party/igeo7_duckdb, upstream:
# https://github.com/allixender/igeo7_duckdb). We compile the Z7 core files
# under src/z7/ and the auth/ math (Karney 2022 Fourier series) and register our
# own DuckDB scalar-function wrappers.
# --------------------------------------------------------------------------
set(IGEO7_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/igeo7_duckdb/src")
set(Z7_DIR "${IGEO7_DIR}/z7")
set(AUTH_DIR "${IGEO7_DIR}/auth")

if(NOT EXISTS "${Z7_DIR}/library.h")
  message(FATAL_ERROR "igeo7_duckdb submodule not found at ${Z7_DIR}.\n"
                      "Run: git submodule update --init --recursive")
endif()

if(NOT EXISTS "${AUTH_DIR}/authalic.hpp")
  message(
    FATAL_ERROR
      "igeo7_duckdb authalic source not found at ${AUTH_DIR}.\n"
      "The submodule pointer is older than the authalic commit; run:\n"
      "  git submodule update --remote third_party/igeo7_duckdb")
endif()

# --------------------------------------------------------------------------
# Extension sources
# --------------------------------------------------------------------------
set(EXTENSION_SOURCES
    src/duck_dggs_extension.cpp src/dggrid_transform.cpp
    src/igeo7_functions.cpp "${Z7_DIR}/library.cpp" "${AUTH_DIR}/authalic.cpp")

# Set LINKED_LIBS so the WASM (Emscripten) post-build emcc command can find
# dglib.  Without this, the emcc -sSIDE_MODULE=2 step does not link dglib and
# static symbols like DgGeoSphRF::icosaEdgeRads_ are left undefined.
set(DUCKDB_EXTENSION_DUCK_DGGS_LINKED_LIBS $<TARGET_FILE:dglib>)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} "" ${EXTENSION_SOURCES})

target_include_directories(${EXTENSION_NAME} PRIVATE "${DGLIB_INCLUDE_DIR}"
                                                     "${Z7_DIR}" "${IGEO7_DIR}")
target_include_directories(
  ${LOADABLE_EXTENSION_NAME} PRIVATE "${DGLIB_INCLUDE_DIR}" "${Z7_DIR}"
                                     "${IGEO7_DIR}")

add_dependencies(${EXTENSION_NAME} dglib)
add_dependencies(${LOADABLE_EXTENSION_NAME} dglib)
target_link_libraries(${EXTENSION_NAME} $<TARGET_FILE:dglib>)
target_link_libraries(${LOADABLE_EXTENSION_NAME} $<TARGET_FILE:dglib>)

# On Linux, the GCC linker (including aarch64) treats duplicate COMDAT symbols
# (C++17 inline statics like LogicalType::DOUBLE) as hard errors when linking
# extension object files against libduckdb_static.a.  These symbols are
# identical across all TUs so multiple definitions are safe to merge. INTERFACE
# on the static extension propagates the flag to the final test binary that
# DuckDB's build system links (libduck_dggs_extension.a + libduckdb_static.a).
# PRIVATE on the loadable extension covers the .so link.
if((UNIX AND NOT APPLE) OR MINGW)
  target_link_options(${EXTENSION_NAME} INTERFACE
                      -Wl,--allow-multiple-definition)
  target_link_options(${LOADABLE_EXTENSION_NAME} PRIVATE
                      -Wl,--allow-multiple-definition)
endif()

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
