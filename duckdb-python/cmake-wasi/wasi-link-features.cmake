# Injected via -DCMAKE_PROJECT_INCLUDE (runs right after the top-level
# project() in duckdb-python's CMakeLists.txt).
#
# DuckDB-Python's duckdb_link_extensions() links the extension loader
# with the generator expression
#   $<LINK_LIBRARY:WHOLE_ARCHIVE,duckdb_generated_extension_loader>
# CMake implements the WHOLE_ARCHIVE LINK_LIBRARY feature per-platform
# (Compiler/Platform modules). The WASI platform module ships no such
# definition, so CMake errors at generate time:
#   Feature 'WHOLE_ARCHIVE' ... is not supported for the 'CXX' link language.
#
# wasm-ld supports --whole-archive / --no-whole-archive, so we define the
# feature ourselves, mirroring CMake's built-in GNU/binutils definition.
# LINKER: expands to the toolchain's linker-flag prefix; <LINK_ITEM> is
# the library placeholder.

foreach(lang C CXX)
  if(NOT CMAKE_${lang}_LINK_LIBRARY_USING_WHOLE_ARCHIVE_SUPPORTED)
    set(CMAKE_${lang}_LINK_LIBRARY_USING_WHOLE_ARCHIVE
        "LINKER:--whole-archive"
        "<LINK_ITEM>"
        "LINKER:--no-whole-archive")
    set(CMAKE_${lang}_LINK_LIBRARY_USING_WHOLE_ARCHIVE_SUPPORTED TRUE)
  endif()
endforeach()
