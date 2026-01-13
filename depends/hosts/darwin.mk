OSX_MIN_VERSION=14.0
OSX_SDK_VERSION=14.0

XCODE_VERSION=15.0
XCODE_BUILD_ID=15A240d
LLD_VERSION=711

# For native macOS builds, use the system SDK
# For cross-compilation, use the custom extracted SDK
ifeq ($(build_os),darwin)
OSX_SDK=$(shell xcrun --show-sdk-path)
else
OSX_SDK=$(SDK_PATH)/Xcode-$(XCODE_VERSION)-$(XCODE_BUILD_ID)-extracted-SDK-with-libcxx-headers
endif

# We can't just use $(shell command -v clang) because GNU Make handles builtins
# in a special way and doesn't know that `command` is a POSIX-standard builtin
# prior to 1af314465e5dfe3e8baa839a32a72e83c04f26ef, first released in v4.2.90.
# At the time of writing, GNU Make v4.2.1 is still being used in supported
# distro releases.
#
# Source: https://lists.gnu.org/archive/html/bug-make/2017-11/msg00017.html
default_clang := $(shell which clang)
default_clangxx := $(shell which clang++)

ifneq ($(build_os),darwin)
clang_prog := $(if $(default_clang),$(default_clang),clang)
clangxx_prog := $(if $(default_clangxx),$(default_clangxx),clang++)
darwin_AR=$(if $(AR),$(AR),llvm-ar)
darwin_DSYMUTIL=$(if $(DSYMUTIL),$(DSYMUTIL),dsymutil)
darwin_NM=$(if $(NM),$(NM),llvm-nm)
darwin_OBJDUMP=$(if $(OBJDUMP),$(OBJDUMP),llvm-objdump)
darwin_RANLIB=$(if $(RANLIB),$(RANLIB),llvm-ranlib)
darwin_STRIP=$(if $(STRIP),$(STRIP),llvm-strip)
else
clang_prog := $(default_clang)
clangxx_prog := $(default_clang++)
darwin_AR:=$(shell which llvm-ar)
darwin_DSYMUTIL:=$(shell which dsymutil)
darwin_NM:=$(shell which llvm-nm)
darwin_OBJDUMP:=$(shell which llvm-objdump)
darwin_RANLIB:=$(shell which llvm-ranlib)
darwin_STRIP:=$(shell which llvm-strip)
endif

# Flag explanations:
#
#     -mlinker-version
#
#         Ensures that modern linker features are enabled. See here for more
#         details: https://github.com/bitcoin/bitcoin/pull/19407.
#
#     -isysroot$(OSX_SDK) -nostdlibinc
#
#         Disable default include paths built into the compiler as well as
#         those normally included for libc and libc++. The only path that
#         remains implicitly is the clang resource dir.
#
#     -iwithsysroot / -iframeworkwithsysroot
#
#         Adds the desired paths from the SDK
#
#     -platform_version
#
#         Indicate to the linker the platform, the oldest supported version,
#         and the SDK used.
#
#     -no_adhoc_codesign
#
#         Disable adhoc codesigning (for now) when using LLVM tooling, to avoid
#         non-determinism issues with the Identifier field.

ifeq ($(build_os),darwin)
# Native macOS build - simpler flags
darwin_CC=$(clang_prog) -mmacosx-version-min=$(OSX_MIN_VERSION) -isysroot $(OSX_SDK)
darwin_CXX=$(clangxx_prog) -mmacosx-version-min=$(OSX_MIN_VERSION) -stdlib=libc++ -isysroot $(OSX_SDK)
darwin_CFLAGS=-pipe -std=$(C_STANDARD)
darwin_CXXFLAGS=-pipe -std=$(CXX_STANDARD)
darwin_LDFLAGS=-Wl,-platform_version,macos,$(OSX_MIN_VERSION),$(OSX_SDK_VERSION)
else
# Cross-compilation build - keep CC/CXX simple, put all flags in CFLAGS/CXXFLAGS
darwin_CC=$(clang_prog)
darwin_CXX=$(clangxx_prog)

darwin_CFLAGS=-pipe -std=$(C_STANDARD) -mmacos-version-min=$(OSX_MIN_VERSION) --target=$(host) \
              -isysroot $(OSX_SDK) \
              -iwithsysroot/usr/include -iframeworkwithsysroot/System/Library/Frameworks

darwin_CXXFLAGS=-pipe -std=$(CXX_STANDARD) -mmacos-version-min=$(OSX_MIN_VERSION) --target=$(host) \
                -isysroot $(OSX_SDK) --stdlib=libc++ \
                -iwithsysroot/usr/include/c++/v1 \
                -iwithsysroot/usr/include -iframeworkwithsysroot/System/Library/Frameworks

darwin_LDFLAGS=-Wl,-platform_version,macos,$(OSX_MIN_VERSION),$(OSX_SDK_VERSION)
endif

ifneq ($(build_os),darwin)
darwin_CFLAGS += -mlinker-version=$(LLD_VERSION)
darwin_CXXFLAGS += -mlinker-version=$(LLD_VERSION)
darwin_LDFLAGS += -Wl,-no_adhoc_codesign -fuse-ld=lld
endif

darwin_release_CFLAGS=-O2
darwin_release_CXXFLAGS=$(darwin_release_CFLAGS)

darwin_debug_CFLAGS=-O1 -g
darwin_debug_CXXFLAGS=$(darwin_debug_CFLAGS)

darwin_cmake_system_name=Darwin
# Darwin version, which corresponds to OSX_MIN_VERSION.
# See https://en.wikipedia.org/wiki/Darwin_(operating_system)
darwin_cmake_system_version=20.1

