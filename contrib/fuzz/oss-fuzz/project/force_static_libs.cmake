# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Wired in via -DCMAKE_PROJECT_Tapyrus_INCLUDE (build.sh) rather than
# passing -DCMAKE_FIND_LIBRARY_SUFFIXES=.a on the cmake command line --
# confirmed directly (a minimal test project in this same base image)
# that project()'s own platform/compiler-detection modules unconditionally
# `set(CMAKE_FIND_LIBRARY_SUFFIXES .so .a)` as a normal variable right
# after project() runs, which shadows a command-line-seeded cache entry
# of the same name for the rest of configure -- the cache entry itself
# stays .a (visible in CMakeCache.txt), but every find_library() call
# still sees .so;.a. CMAKE_PROJECT_<name>_INCLUDE is CMake's own hook for
# exactly this: a script auto-included immediately after the matching
# project() command, late enough that a plain set() here isn't clobbered
# by anything project() already did.
set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
