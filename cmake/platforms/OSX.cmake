# Copyright (c) 2017 The Bitcoin developers

set(CMAKE_SYSTEM_NAME Darwin)
if(HOST_ARCH STREQUAL "arm64")
	set(TOOLCHAIN_PREFIX "aarch64-apple-darwin")
else()
	set(TOOLCHAIN_PREFIX "x86_64-apple-darwin")
endif()

# On OSX, we use clang by default.
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
# set(CMAKE_CXX_EXTENSIONS OFF)

# On OSX we use various stuff from Apple's SDK.
OSX_SDK_BASENAME="Xcode-16.2-16C5032a-extracted-SDK-with-libcxx-headers"

set(OSX_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/depends/SDKs/MacOSX15.2.sdk")

# target environment on the build host system
#   set 1st to dir with the cross compiler's C/C++ headers/libs
set(CMAKE_FIND_ROOT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/depends/${TOOLCHAIN_PREFIX};${OSX_SDK_PATH}")

# We also may have built dependancies for the native plateform.
set(CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/depends/${TOOLCHAIN_PREFIX}/native")

# modify default behavior of FIND_XXX() commands to
# search for headers/libs in the target environment and
# search for programs in the build host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Sysroot clang
set(OSX_EXTRA_FLAGS
	" -target ${TOOLCHAIN_PREFIX}"
	" -mmacosx-version-min=11"
	" --sysroot ${OSX_SDK_PATH}"
	" -mlinker-version=711"
)

string(APPEND CMAKE_C_FLAGS ${OSX_EXTRA_FLAGS})
string(APPEND CMAKE_CXX_FLAGS ${OSX_EXTRA_FLAGS} " -stdlib=libc++")

# Ensure we use an OSX specific version of ar, ranlib and nm.
find_program(CMAKE_AR ${TOOLCHAIN_PREFIX}-ar)
find_program(CMAKE_RANLIB ${TOOLCHAIN_PREFIX}-ranlib)
find_program(CMAKE_NM ${TOOLCHAIN_PREFIX}-nm)
