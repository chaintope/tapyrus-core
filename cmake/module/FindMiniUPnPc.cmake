# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

#[=======================================================================[
FindMiniUPnPc
-------------

Finds the MiniUPnPc headers and library.

This is a wrapper around find_package()/pkg_check_modules() commands that:
 - facilitates searching in various build environments
 - prints a standard log message
 - enforces the minimum MiniUPnPc API version tapyrus-core requires

#]=======================================================================]

if(NOT MSVC)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(PC_MiniUPnPc QUIET miniupnpc)
endif()

find_path(MiniUPnPc_INCLUDE_DIR
  NAMES miniupnpc/miniupnpc.h
  PATHS ${PC_MiniUPnPc_INCLUDE_DIRS}
)

if(MiniUPnPc_INCLUDE_DIR)
  file(
    STRINGS "${MiniUPnPc_INCLUDE_DIR}/miniupnpc/miniupnpc.h" version_strings
    REGEX "^#define[\t ]+MINIUPNPC_API_VERSION[\t ]+[0-9]+"
  )
  string(REGEX REPLACE "^#define[\t ]+MINIUPNPC_API_VERSION[\t ]+([0-9]+)" "\\1" MiniUPnPc_API_VERSION "${version_strings}")

  # The minimum supported miniUPnPc API version is set to 17. This excludes
  # versions with known vulnerabilities.
  if(MiniUPnPc_API_VERSION GREATER_EQUAL 17)
    set(MiniUPnPc_API_VERSION_OK TRUE)
  endif()
endif()

if(MSVC)
  cmake_path(GET MiniUPnPc_INCLUDE_DIR PARENT_PATH MiniUPnPc_IMPORTED_PATH)
  find_library(MiniUPnPc_LIBRARY_DEBUG
    NAMES miniupnpc PATHS ${MiniUPnPc_IMPORTED_PATH}/debug/lib
    NO_DEFAULT_PATH
  )
  find_library(MiniUPnPc_LIBRARY_RELEASE
    NAMES miniupnpc PATHS ${MiniUPnPc_IMPORTED_PATH}/lib
    NO_DEFAULT_PATH
  )
  set(MiniUPnPc_required MiniUPnPc_IMPORTED_PATH)
else()
  find_library(MiniUPnPc_LIBRARY
    NAMES miniupnpc
    PATHS ${PC_MiniUPnPc_LIBRARY_DIRS}
  )
  set(MiniUPnPc_required MiniUPnPc_LIBRARY)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MiniUPnPc
  REQUIRED_VARS ${MiniUPnPc_required} MiniUPnPc_INCLUDE_DIR MiniUPnPc_API_VERSION_OK
)

if(MiniUPnPc_FOUND AND NOT TARGET MiniUPnPc::MiniUPnPc)
  add_library(MiniUPnPc::MiniUPnPc UNKNOWN IMPORTED)
  set_target_properties(MiniUPnPc::MiniUPnPc PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${MiniUPnPc_INCLUDE_DIR}"
  )
  if(MSVC)
    if(MiniUPnPc_LIBRARY_DEBUG)
      set_property(TARGET MiniUPnPc::MiniUPnPc APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
      set_target_properties(MiniUPnPc::MiniUPnPc PROPERTIES
        IMPORTED_LOCATION_DEBUG "${MiniUPnPc_LIBRARY_DEBUG}"
      )
    endif()
    if(MiniUPnPc_LIBRARY_RELEASE)
      set_property(TARGET MiniUPnPc::MiniUPnPc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
      set_target_properties(MiniUPnPc::MiniUPnPc PROPERTIES
        IMPORTED_LOCATION_RELEASE "${MiniUPnPc_LIBRARY_RELEASE}"
      )
    endif()
  else()
    set_target_properties(MiniUPnPc::MiniUPnPc PROPERTIES
      IMPORTED_LOCATION "${MiniUPnPc_LIBRARY}"
    )
  endif()
  # USE_UPNP itself is not set here (unlike upstream Bitcoin Core's version
  # of this module) -- tapyrus-core threads it through
  # cmake/tapyrus-build-config.h.in's #cmakedefine01 USE_UPNP instead, the
  # same mechanism USE_QRCODE/USE_DBUS use, so CMakeLists.txt's
  # `set(USE_UPNP TRUE)` next to this find_package() call is the one place
  # that turns it on.
  set_property(TARGET MiniUPnPc::MiniUPnPc PROPERTY
    INTERFACE_COMPILE_DEFINITIONS $<$<PLATFORM_ID:Windows>:MINIUPNP_STATICLIB>
  )
endif()

mark_as_advanced(
  MiniUPnPc_INCLUDE_DIR
  MiniUPnPc_LIBRARY
)
