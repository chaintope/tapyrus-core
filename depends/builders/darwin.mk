# Normalize the build triplet so native-build detection works.
# config.guess on Apple Silicon returns aarch64-apple-darwin[version]; the CI
# sets HOST=arm64-apple-darwin (from uname -m, no version).  Two normalizations:
#   1. Strip the Darwin version suffix (24.5.0 → nothing)
#   2. Rename aarch64 → arm64 to match the HOST naming convention
# Use pure make variable operations (build_arch, build_vendor) instead of a
# $(shell sed ...) pipeline: the pipeline is sensitive to which sed is first in
# PATH after Homebrew auto-updates, causing intermittent CI failures.
ifeq ($(build_arch),aarch64)
build:=arm64-$(build_vendor)-darwin
else
build:=$(build_arch)-$(build_vendor)-darwin
endif

build_darwin_CC:=$(shell xcrun -f clang) -isysroot$(shell xcrun --show-sdk-path)
build_darwin_CXX:=$(shell xcrun -f clang++) -isysroot$(shell xcrun --show-sdk-path)
build_darwin_AR:=$(shell xcrun -f ar)
build_darwin_RANLIB:=$(shell xcrun -f ranlib)
build_darwin_STRIP:=$(shell xcrun -f strip)
build_darwin_OTOOL:=$(shell xcrun -f otool)
build_darwin_NM:=$(shell xcrun -f nm)
build_darwin_SHA256SUM=shasum -a 256
build_darwin_DOWNLOAD=curl --location --fail --connect-timeout $(DOWNLOAD_CONNECT_TIMEOUT) --retry $(DOWNLOAD_RETRIES) -o

#darwin host on darwin builder. overrides darwin host preferences.
darwin_CC=$(shell xcrun -f clang) -mmacosx-version-min=$(OSX_MIN_VERSION) -isysroot$(shell xcrun --show-sdk-path)
darwin_CXX:=$(shell xcrun -f clang++) -mmacosx-version-min=$(OSX_MIN_VERSION) -stdlib=libc++ -isysroot$(shell xcrun --show-sdk-path)
darwin_AR:=$(shell xcrun -f ar)
darwin_RANLIB:=$(shell xcrun -f ranlib)
darwin_STRIP:=$(shell xcrun -f strip)
darwin_OTOOL:=$(shell xcrun -f otool)
darwin_NM:=$(shell xcrun -f nm)

x86_64_darwin_CFLAGS += -arch x86_64
x86_64_darwin_CXXFLAGS += -arch x86_64
arm64_darwin_CFLAGS += -arch arm64
arm64_darwin_CXXFLAGS += -arch arm64