# Copyright (c) 2024-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

#[=======================================================================[
FindZeroMQ
----------

Finds the ZeroMQ headers and library.

This is a wrapper around find_package()/pkg_check_modules() commands that:
 - facilitates searching in various build environments
 - prints a standard log message

#]=======================================================================]

include(FindPackageHandleStandardArgs)

# Set CMP0144 policy to use upper-case <PACKAGENAME>_ROOT variables
if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()

# Add Homebrew paths for macOS only when not using depends build and ZEROMQ_ROOT not set
if(APPLE AND NOT CMAKE_TOOLCHAIN_FILE AND NOT ZEROMQ_ROOT)
  list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
  list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/zeromq")
  set(ZEROMQ_ROOT "/opt/homebrew/opt/zeromq")
  set(ZEROMQ_INCLUDE_DIR "${ZEROMQ_ROOT}/include")
  set(ZEROMQ_LIBRARY "${ZEROMQ_ROOT}/lib/libzmq.dylib")
endif()

# Try to find ZeroMQ through various methods
find_package(ZeroMQ ${ZeroMQ_FIND_VERSION} NO_MODULE QUIET)
if(ZeroMQ_FOUND)
  find_package_handle_standard_args(ZeroMQ
    REQUIRED_VARS ZeroMQ_DIR
    VERSION_VAR ZeroMQ_VERSION
  )
  if(TARGET libzmq)
    add_library(zeromq ALIAS libzmq)
  elseif(TARGET libzmq-static)
    add_library(zeromq ALIAS libzmq-static)
  endif()
  mark_as_advanced(ZeroMQ_DIR)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(libzmq QUIET
    IMPORTED_TARGET
    libzmq>=${ZeroMQ_FIND_VERSION}
  )
  if(TARGET PkgConfig::libzmq)
    add_library(zeromq ALIAS PkgConfig::libzmq)
  else()
    # Create an imported target for ZeroMQ if pkg-config didn't create one
    # First, try to use explicitly set ZEROMQ_ROOT if available
    if(ZEROMQ_ROOT)
      # For depends builds, prioritize the explicitly set paths
      if(NOT ZEROMQ_INCLUDE_DIR)
        set(ZEROMQ_INCLUDE_DIR "${ZEROMQ_ROOT}/include")
      endif()
      if(NOT ZEROMQ_LIBRARY)
        # Look for static library first (preferred for depends), then dynamic
        find_library(ZEROMQ_LIBRARY
          NAMES zmq libzmq
          HINTS "${ZEROMQ_ROOT}/lib"
          PATHS "${ZEROMQ_ROOT}/lib"
          NO_DEFAULT_PATH
        )
      endif()
    elseif(APPLE AND EXISTS "${ZEROMQ_LIBRARY}")
      # Fallback for macOS homebrew when ZEROMQ_ROOT not explicitly set
    else()
      # System library search
      find_library(ZEROMQ_LIBRARY NAMES zmq)
      find_path(ZEROMQ_INCLUDE_DIR NAMES zmq.h)
    endif()

    # Create the imported target if we found the library and headers
    if(ZEROMQ_LIBRARY AND ZEROMQ_INCLUDE_DIR AND EXISTS "${ZEROMQ_LIBRARY}" AND EXISTS "${ZEROMQ_INCLUDE_DIR}/zmq.h")
      add_library(zeromq UNKNOWN IMPORTED)
      set_target_properties(zeromq PROPERTIES
        IMPORTED_LOCATION "${ZEROMQ_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ZEROMQ_INCLUDE_DIR}"
      )
    endif()
  endif()
  if(TARGET zeromq)
    set(ZeroMQ_FOUND TRUE)
  else()
    set(ZeroMQ_FOUND FALSE)
  endif()

  find_package_handle_standard_args(ZeroMQ
    REQUIRED_VARS ZeroMQ_FOUND
    VERSION_VAR libzmq_VERSION
  )
endif()
