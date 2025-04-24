# Copyright (c) 2018 The Bitcoin developers

cmake_minimum_required(VERSION 3.22)

if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "In-source builds are not allowed.")
endif()

#=============================
# Project / Package metadata
#=============================
set(CLIENT_NAME "Tapyrus Core")
set(CLIENT_VERSION_MAJOR 0)
set(CLIENT_VERSION_MINOR 6)
set(CLIENT_VERSION_BUILD 1)
set(CLIENT_VERSION_RC 0)
set(CLIENT_VERSION_IS_RELEASE "false")

# During the enabling of the CXX and CXXOBJ languages, we modify
# CMake's compiler/linker invocation strings by appending the content
# of the user-defined `APPEND_*` variables, which allows overriding
# any flag. We also ensure that the APPEND_* flags are considered
# during CMake's tests, which use the `try_compile()` command.
#
# CMake's docs state that the `CMAKE_TRY_COMPILE_PLATFORM_VARIABLES`
# variable "is meant to be set by CMake's platform information modules
# for the current toolchain, or by a toolchain file." We do our best
# to set it before the `project()` command.
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
        CMAKE_CXX_COMPILE_OBJECT
        CMAKE_OBJCXX_COMPILE_OBJECT
        CMAKE_CXX_LINK_EXECUTABLE
)

project(Tapyrus
        VERSION ${CLIENT_VERSION_MAJOR}.${CLIENT_VERSION_MINOR}.${CLIENT_VERSION_BUILD}
        DESCRIPTION "Tapyrus client software"
        HOMEPAGE_URL "https://www.chaintope.com"
        LANGUAGES C CXX
)

set(CLIENT_VERSION_STRING ${PROJECT_VERSION})
if(CLIENT_VERSION_RC GREATER 0)
    string(APPEND CLIENT_VERSION_STRING "rc${CLIENT_VERSION_RC}")
endif()

# Copyright
set(COPYRIGHT_YEAR 2019)
set(COPYRIGHT_HOLDERS_FINAL "The Bitcoin Core developers")
set(CLIENT_BUGREPORT "https://github.com/chaintope/tapyrus-core/issues")

#=============================
# Language setup
#=============================
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_HOST_APPLE)
    # We do not use the install_name_tool when cross-compiling for macOS.
    # So disable this tool check in further enable_language() commands.
    set(CMAKE_PLATFORM_HAS_INSTALLNAME FALSE)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C standard can be overridden when this is used as a sub-project.
if(NOT CMAKE_C_STANDARD)
    # This project can use C11, but will gracefully decay down to C89.
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED OFF)
    set(CMAKE_C_EXTENSIONS OFF)
endif(NOT CMAKE_C_STANDARD)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/module)

#=============================
# Configurable options
#=============================
include(CMakeDependentOption)

# Default visibility is hidden on all targets.
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

option(BUILD_TAPYRUS_DAEMON "Build tapyrusd" ON)
option(BUILD_TAPYRUS_CLI "Build tapyrus-cli" ON)
option(BUILD_TAPYRUS_GENESIS "Build tapyrus-genesis" ON)
option(BUILD_TAPYRUS_TX "Build tapyrus-tx" ON)
option(BUILD_TAPYRUS_QT "Build tapyrus-qt" OFF)
option(ENABLE_QRCODE "Build QR support in wallet" OFF)
option(BUILD_TAPYRUS_WALLET "Activate the wallet functionality" ON)
option(BUILD_TAPYRUS_ZMQ "Activate the ZeroMQ functionalities" ON)
option(BUILD_TEST "Build tapyrus unit test" ON)
option(BUILD_BENCH "Build benchmark" ON)

# Add path for custom modules
set(CMAKE_MODULE_PATH
        ${CMAKE_MODULE_PATH}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules
        )

# Make contrib script accessible.
set(CONTRIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/contrib)

# If ccache is available, then use it.
find_program(CCACHE ccache)
if(CCACHE)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ${CCACHE})
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ${CCACHE})
endif(CCACHE)

# The core_interface library aims to encapsulate common build flags.
# It is a usage requirement for all targets except for secp256k1, which
# gets its flags by other means.
add_library(core_interface INTERFACE)
add_library(core_interface_relwithdebinfo INTERFACE)
add_library(core_interface_debug INTERFACE)
target_link_libraries(core_interface INTERFACE
        $<$<CONFIG:RelWithDebInfo>:core_interface_relwithdebinfo>
        $<$<CONFIG:Debug>:core_interface_debug>
)

# Add the magic taret check and check-all
add_custom_target(check-all)
add_custom_target(check)

add_subdirectory(src)
add_subdirectory(test)

add_macos_deploy_target()

message("\n")
message("Configure summary")
message("=================")
message("Executables:")
message("  tapyrusd ............................ ${BUILD_TAPYRUS_DAEMON}")
message("  tapyrus-cli ......................... ${BUILD_TAPYRUS_CLI}")
message("  tapyrus-genesis ..................... ${BUILD_TAPYRUS_GENESIS}")
message("  tapyrus-tx .......................... ${BUILD_TAPYRUS_TX}")
message("  tapyrus-qt (GUI) .................... ${BUILD_TAPYRUS_QT}")
message("    QR code in GUI .................... ${ENABLE_QRCODE}")
message("  wallet support ...................... ${BUILD_TAPYRUS_WALLET}")
message("  ZeroMQ .............................. ${BUILD_TAPYRUS_ZMQ}")
message("  USDT tracing ........................ ${WITH_USDT}")
message("Tests:")
message("  test_tapyrus ........................ ${BUILD_TEST}")
message("  test_tapyrus-qt ..................... ${BUILD_GUI_TESTS}")
message("  bench_tapyrus ....................... ${BUILD_BENCH}")
message("")
if(CMAKE_CROSSCOMPILING)
    set(cross_status "TRUE, for ${CMAKE_SYSTEM_NAME}, ${CMAKE_SYSTEM_PROCESSOR}")
else()
    set(cross_status "FALSE")
endif()
message("Cross compiling ....................... ${cross_status}")
message("C++ compiler .......................... ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}, ${CMAKE_CXX_COMPILER}")
message("C compiler .......................... ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}, ${CMAKE_C_COMPILER}")
include(FlagsSummary)
flags_summary()
message("Attempt to harden executables ......... ${ENABLE_HARDENING}")
message("Treat compiler warnings as errors ..... ${WERROR}")
message("Use ccache for compiling .............. ${WITH_CCACHE}")
message("\n")
if(configure_warnings)
    message("  ******\n")
    foreach(warning IN LISTS configure_warnings)
        message(WARNING "${warning}")
    endforeach()
    message("  ******\n")
endif()

# We want all build properties to be encapsulated properly.
include(WarnAboutGlobalProperties)
