# Normalize the build triplet so native-build detection works.
# config.guess on Apple Silicon returns aarch64-apple-darwin25.x.x; the CI sets
# HOST=arm64-apple-darwin (from uname -m).  Two normalizations are needed:
#   1. Strip the Darwin version suffix (24.5.0 → nothing)
#   2. Rename aarch64 → arm64 to match the HOST naming convention
# Without both, $(host) != $(build) and a native arm64 build is wrongly treated
# as cross-compilation, causing the GNU-only `sed -i` in qt.mk to run under
# BSD sed and fail with "extra characters at the end of q command".
build:=$(shell echo "$(build)" | sed 's/\(.*-apple-darwin\)[0-9.]*$$/\1/' | sed 's/^aarch64-apple-darwin$$/arm64-apple-darwin/')

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