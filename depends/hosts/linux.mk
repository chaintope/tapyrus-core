linux_CFLAGS=-pipe
linux_CXXFLAGS=$(linux_CFLAGS)
linux_LDFLAGS=-static-libgcc -static-libstdc++

linux_release_CFLAGS=-O2
linux_release_CXXFLAGS=$(linux_release_CFLAGS)

linux_debug_CFLAGS=-O1
linux_debug_CXXFLAGS=$(linux_debug_CFLAGS)

linux_debug_CPPFLAGS=-D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC

ifeq (86,$(findstring 86,$(build_arch)))
i686_linux_CC=gcc -m32
i686_linux_CXX=g++ -m32
i686_linux_AR=ar
i686_linux_RANLIB=ranlib
i686_linux_NM=nm
i686_linux_STRIP=strip

x86_64_linux_CC=gcc
x86_64_linux_CXX=g++
x86_64_linux_AR=ar
x86_64_linux_RANLIB=ranlib
x86_64_linux_NM=nm
x86_64_linux_STRIP=strip
else
i686_linux_CC=$(default_host_CC) -m32
i686_linux_CXX=$(default_host_CXX) -m32
endif

# For x86_64-linux-gnu, always use gcc (not cross-compiler) since it's native in containers
x86_64_linux_CC=gcc
x86_64_linux_CXX=g++
x86_64_linux_AR=ar
x86_64_linux_RANLIB=ranlib
x86_64_linux_NM=nm
x86_64_linux_STRIP=strip

# AArch64 (ARM64) cross-compilation
aarch64_linux_CC=aarch64-linux-gnu-gcc
aarch64_linux_CXX=aarch64-linux-gnu-g++
aarch64_linux_AR=aarch64-linux-gnu-ar
aarch64_linux_RANLIB=aarch64-linux-gnu-ranlib
aarch64_linux_NM=aarch64-linux-gnu-nm
aarch64_linux_STRIP=aarch64-linux-gnu-strip

linux_cmake_system_name=Linux
# Refer to doc/dependencies.md for the minimum required kernel.
linux_cmake_system_version=3.17.0
