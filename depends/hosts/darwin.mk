OSX_MIN_VERSION=11.0
OSX_SDK_VERSION=15.0

clang_prog=$(build_prefix)/bin/clang
clangxx_prog=$(clang_prog)++
llvm_config_prog=$(build_prefix)/bin/llvm-config

clang_resource_dir=$(build_prefix)/lib/clang/$(native_cctools_clang_version)

darwin_CC=$(clang_prog) -target $(host)  -isysroot$(OSX_SDK)
darwin_CXX=$(clangxx_prog) -target $(host) -isysroot$(OSX_SDK) -stdlib=libc++

darwin_CFLAGS=-pipe -std=c11
darwin_CXXFLAGS=-pipe -std=c++17

darwin_release_CFLAGS=-O2
darwin_release_CXXFLAGS=$(darwin_release_CFLAGS)

darwin_debug_CFLAGS=-O1
darwin_debug_CXXFLAGS=$(darwin_debug_CFLAGS)

darwin_native_toolchain=native_cctools
