# Copyright (c) 2024-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

#[=======================================================================[
FindLibevent
------------

Finds the Libevent headers and libraries.

This is a wrapper around find_package()/pkg_check_modules() commands that:
 - facilitates searching in various build environments
 - prints a standard log message

#]=======================================================================]

# Check whether evhttp_connection_get_peer expects const char**.
# See https://github.com/libevent/libevent/commit/a18301a2bb160ff7c3ffaf5b7653c39ffe27b385
function(check_evhttp_connection_get_peer target)
  include(CMakePushCheckState)
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_LIBRARIES ${target})
  include(CheckCXXSourceCompiles)
  check_cxx_source_compiles("
    #include <cstdint>
    #include <event2/http.h>

    int main()
    {
        evhttp_connection* conn = (evhttp_connection*)1;
        const char* host;
        uint16_t port;
        evhttp_connection_get_peer(conn, &host, &port);
    }
    " HAVE_EVHTTP_CONNECTION_GET_PEER_CONST_CHAR
  )
  cmake_pop_check_state()
  target_compile_definitions(${target} INTERFACE
    $<$<BOOL:${HAVE_EVHTTP_CONNECTION_GET_PEER_CONST_CHAR}>:HAVE_EVHTTP_CONNECTION_GET_PEER_CONST_CHAR>
  )
endfunction()

set(_libevent_components core extra)
if(NOT WIN32)
  list(APPEND _libevent_components pthreads)
endif()

# Try CMake config first
find_package(Libevent ${Libevent_FIND_VERSION} QUIET
  COMPONENTS ${_libevent_components}
  NO_MODULE
)

include(FindPackageHandleStandardArgs)
if(Libevent_FOUND)
  find_package_handle_standard_args(Libevent
    REQUIRED_VARS Libevent_DIR
    VERSION_VAR Libevent_VERSION
  )
  check_evhttp_connection_get_peer(libevent::extra)
else()
  # Manual search for system libraries
  foreach(component IN LISTS _libevent_components)
    find_library(Libevent_${component}_LIBRARY
      NAMES event_${component}
    )
    find_path(Libevent_INCLUDE_DIR
      NAMES event2/event.h
    )

    if(Libevent_${component}_LIBRARY AND Libevent_INCLUDE_DIR)
      if(NOT TARGET libevent::${component})
        add_library(libevent::${component} UNKNOWN IMPORTED)
        set_target_properties(libevent::${component} PROPERTIES
          IMPORTED_LOCATION "${Libevent_${component}_LIBRARY}"
          INTERFACE_INCLUDE_DIRECTORIES "${Libevent_INCLUDE_DIR}"
        )
      endif()
    endif()
  endforeach()

  find_package_handle_standard_args(Libevent
    REQUIRED_VARS Libevent_core_LIBRARY Libevent_INCLUDE_DIR
  )

  if(Libevent_FOUND)
    check_evhttp_connection_get_peer(libevent::extra)
  endif()
endif()

unset(_libevent_components)

mark_as_advanced(Libevent_DIR)
mark_as_advanced(_event_h)
mark_as_advanced(_event_lib)
