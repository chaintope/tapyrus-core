# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

function(add_boost_if_needed)
  #[=[
  TODO: Not all targets, which will be added in the future, require
        Boost. Therefore, a proper check will be appropriate here.

  Implementation notes:
  Although only Boost headers are used to build Bitcoin Core,
  we still leverage a standard CMake's approach to handle
  dependencies, i.e., the Boost::headers "library".
  A command target_link_libraries(target PRIVATE Boost::headers)
  will propagate Boost::headers usage requirements to the target.
  For Boost::headers such usage requirements is an include
  directory and other added INTERFACE properties.
  ]=]

  # We cannot rely on find_package(Boost ...) to work properly without
  # Boost_NO_BOOST_CMAKE set until we require a more recent Boost because
  # upstream did not ship proper CMake files until 1.82.0.
  # Until then, we rely on CMake's FindBoost module.
  # See: https://cmake.org/cmake/help/latest/policy/CMP0167.html
  if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
  endif()
  set(Boost_NO_BOOST_CMAKE ON)

  # Set policy for BOOST_ROOT variable handling
  if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
  endif()

  # For depends builds, ensure we look for static libraries
  if(DEFINED ENV{BOOST_ROOT} OR DEFINED BOOST_ROOT)
    set(Boost_USE_STATIC_LIBS ON)
    set(Boost_USE_MULTITHREADED ON)
    set(Boost_USE_STATIC_RUNTIME OFF)
    # Set additional paths that depends might use
    if(DEFINED BOOST_ROOT)
      # Only set BOOST_LIBRARYDIR and BOOST_INCLUDEDIR if not already explicitly set
      if(NOT DEFINED BOOST_LIBRARYDIR)
        set(BOOST_LIBRARYDIR "${BOOST_ROOT}/lib")
      endif()
      if(NOT DEFINED BOOST_INCLUDEDIR)
        set(BOOST_INCLUDEDIR "${BOOST_ROOT}/include")
      endif()

      # Only validate BOOST_ROOT if it wasn't explicitly set via command line
      # (i.e., only validate environment-set BOOST_ROOT)
      if(DEFINED ENV{BOOST_ROOT} AND NOT DEFINED CACHE{BOOST_ROOT})
        # Check if BOOST_ROOT actually contains Boost libraries
        file(GLOB BOOST_HEADERS "${BOOST_INCLUDEDIR}/boost/config.hpp")
        if(NOT BOOST_HEADERS)
          message(STATUS "BOOST_ROOT set but no Boost headers found, unsetting BOOST_ROOT to search system paths")
          unset(BOOST_ROOT)
          unset(BOOST_LIBRARYDIR)
          unset(BOOST_INCLUDEDIR)
          unset(ENV{BOOST_ROOT})
          # Also unset Boost_USE_STATIC_LIBS to allow system dynamic libraries
          unset(Boost_USE_STATIC_LIBS)
        endif()
      endif()
    endif()
  endif()

  # Since we only use Boost headers, find Boost without requiring specific components
  message(STATUS "Finding Boost headers (header-only usage)...")

  # Debug: Show what variables are set before calling find_package
  if(DEFINED BOOST_ROOT)
    message(STATUS "BOOST_ROOT is set to: ${BOOST_ROOT}")
  endif()
  if(DEFINED BOOST_INCLUDEDIR)
    message(STATUS "BOOST_INCLUDEDIR is set to: ${BOOST_INCLUDEDIR}")
  endif()
  if(DEFINED BOOST_LIBRARYDIR)
    message(STATUS "BOOST_LIBRARYDIR is set to: ${BOOST_LIBRARYDIR}")
  endif()
  if(DEFINED ENV{BOOST_ROOT})
    message(STATUS "Environment BOOST_ROOT is set to: $ENV{BOOST_ROOT}")
  endif()

  # If BOOST_INCLUDEDIR is set and contains boost headers, set Boost_INCLUDE_DIR as well
  # This helps CMake's FindBoost module when it's having trouble with BOOST_ROOT
  if(DEFINED BOOST_INCLUDEDIR AND EXISTS "${BOOST_INCLUDEDIR}/boost/config.hpp")
    set(Boost_INCLUDE_DIR "${BOOST_INCLUDEDIR}")
    message(STATUS "Setting Boost_INCLUDE_DIR to: ${Boost_INCLUDE_DIR}")
  endif()

  find_package(Boost 1.73.0 REQUIRED)
  
  mark_as_advanced(Boost_INCLUDE_DIR)
  set_target_properties(Boost::headers PROPERTIES IMPORTED_GLOBAL TRUE)
  target_compile_definitions(Boost::headers INTERFACE
    # We don't use multi_index serialization.
    BOOST_MULTI_INDEX_DISABLE_SERIALIZATION
  )
  if(DEFINED VCPKG_TARGET_TRIPLET)
    # Workaround for https://github.com/microsoft/vcpkg/issues/36955.
    target_compile_definitions(Boost::headers INTERFACE
      BOOST_NO_USER_CONFIG
    )
  endif()

  # Prevent use of std::unary_function, which was removed in C++17,
  # and will generate warnings with newer compilers for Boost
  # older than 1.80.
  # See: https://github.com/boostorg/config/pull/430.
  set(CMAKE_REQUIRED_DEFINITIONS -DBOOST_NO_CXX98_FUNCTION_BASE)
  set(CMAKE_REQUIRED_INCLUDES ${Boost_INCLUDE_DIR})
  include(CMakePushCheckState)
  cmake_push_check_state()
  include(TryAppendCXXFlags)
  set(CMAKE_REQUIRED_FLAGS ${working_compiler_werror_flag})
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  check_cxx_source_compiles("
    #include <boost/config.hpp>
    " NO_DIAGNOSTICS_BOOST_NO_CXX98_FUNCTION_BASE
  )
  cmake_pop_check_state()
  if(NO_DIAGNOSTICS_BOOST_NO_CXX98_FUNCTION_BASE)
    target_compile_definitions(Boost::headers INTERFACE
      BOOST_NO_CXX98_FUNCTION_BASE
    )
  else()
    set(CMAKE_REQUIRED_DEFINITIONS)
  endif()

  # Some package managers, such as vcpkg, vendor Boost.Test separately
  # from the rest of the headers, so we have to check for it individually.
  if(ENABLE_TESTS AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND CMAKE_REQUIRED_DEFINITIONS -DBOOST_TEST_NO_MAIN)
    include(CheckIncludeFileCXX)
    check_include_file_cxx(boost/test/unit_test.hpp HAVE_BOOST_INCLUDED_UNIT_TEST_H)
    if(NOT HAVE_BOOST_INCLUDED_UNIT_TEST_H)
      message(FATAL_ERROR "Building test_bitcoin executable requested but boost/test/unit_test.hpp header not available.")
    endif()
  endif()

endfunction()
