# Copyright (c) 2024-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

include_guard(GLOBAL)

# Generate a per-binary VERSIONINFO .rc file from the shared
# src/tapyrus-win-res.rc.in template, substituting the binary's name and
# description. Sets ${out_var} in the caller's scope to the generated file's
# path so it can be passed straight to add_windows_resources().
#
# Optional keyword args:
#   PRODUCT_NAME <name>  - StringFileInfo ProductName, if it differs from the
#                           binary name (e.g. the GUI binary is "tapyrus-qt"
#                           but its product name is the "Tapyrus Core" brand).
#                           Defaults to binary_name.
#   ICON <path>           - absolute path to a .ico file to embed as IDI_ICON1
#                           (only the GUI binary needs this). Omitted entirely
#                           if not given.
function(configure_windows_rc binary_name file_description out_var)
  cmake_parse_arguments(RC "" "PRODUCT_NAME;ICON" "" ${ARGN})
  if(NOT RC_PRODUCT_NAME)
    set(RC_PRODUCT_NAME ${binary_name})
  endif()
  set(TAPYRUS_RC_BINARY_NAME ${binary_name})
  set(TAPYRUS_RC_FILE_DESCRIPTION ${file_description})
  set(TAPYRUS_RC_PRODUCT_NAME ${RC_PRODUCT_NAME})
  if(RC_ICON)
    set(TAPYRUS_RC_ICON_LINE "IDI_ICON1 ICON DISCARDABLE \"${RC_ICON}\"")
  else()
    set(TAPYRUS_RC_ICON_LINE "")
  endif()
  set(rc_file ${CMAKE_CURRENT_BINARY_DIR}/${binary_name}-res.rc)
  # Always resolve the shared template relative to the project root, not the
  # caller's CMAKE_CURRENT_SOURCE_DIR, since this is called from multiple
  # subdirectories (src/, src/qt/).
  configure_file(${PROJECT_SOURCE_DIR}/src/tapyrus-win-res.rc.in ${rc_file} @ONLY)
  set(${out_var} ${rc_file} PARENT_SCOPE)
endfunction()

macro(add_windows_resources target rc_file)
  if(WIN32)
    target_sources(${target} PRIVATE ${rc_file})
    set_property(SOURCE ${rc_file}
      APPEND PROPERTY COMPILE_DEFINITIONS WINDRES_PREPROC
    )
    # Disable automatic include directory propagation for .rc files
    # because windres cannot handle CMake generator expressions.
    # The generated .rc files only include <windows.h>, which windres
    # finds via its own toolchain search path.
    set_source_files_properties(${rc_file} PROPERTIES
      INCLUDE_DIRECTORIES ""
      COMPILE_OPTIONS ""
    )
  endif()
endmacro()
