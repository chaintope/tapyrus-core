CMake Build Options for Tapyrus Core
====================================

This document provides a comprehensive overview of all CMake configuration options available when building Tapyrus Core.

## Basic Usage

Configure and build with default options:
```bash
cmake -S . -B build
cmake --build build
```

Use `-D<OPTION>=<VALUE>` to customize the build:
```bash
cmake -S . -B build -DBUILD_GUI=OFF -DENABLE_WALLET=OFF
cmake --build build
```

## Executable Control Options

Control which executables are built:

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_DAEMON` | `ON` | Build tapyrusd executable |
| `BUILD_GUI` | `OFF` | Build tapyrus-qt GUI executable |
| `BUILD_CLI` | `ON` | Build tapyrus-cli executable |
| `BUILD_GENESIS` | `ON` | Build tapyrus-genesis executable |
| `BUILD_UTILS` | `${ENABLE_TESTS}` | Build tapyrus-tx utility |

## Feature Control Options

Enable or disable major features:

| Option | Default | Description |
|--------|---------|-------------|
| `ENABLE_WALLET` | `ON` | Build wallet functionality |
| `WITH_BDB` | `ON` | Enable Berkeley DB wallet support |
| `WARN_INCOMPATIBLE_BDB` | `ON` | Warn when using BDB version other than 4.8 |
| `WITH_INCOMPATIBLE_BDB` | `OFF` | Allow incompatible BDB versions |
| `ENABLE_ZMQ` | `ON` | Enable ZMQ notifications |
| `ENABLE_TRACING` | `OFF` | Enable USDT tracepoints |
| `ENABLE_EXTERNAL_SIGNER` | `ON` (non-Windows) | Enable external signer support |

## Testing and Development Options

Control test and benchmark builds:

| Option | Default | Description |
|--------|---------|-------------|
| `ENABLE_TESTS` | `ON` | Build test_tapyrus executable |
| `ENABLE_BENCH` | `ON` | Build bench_tapyrus executable |
| `ENABLE_GUI_TESTS` | `ON` (if GUI enabled) | Build test_tapyrus-qt executable |

## GUI-Specific Options

Options that affect the GUI build:

| Option | Default | Description |
|--------|---------|-------------|
| `WITH_QRENCODE` | `ON` (if GUI enabled) | Enable QR code support |
| `WITH_DBUS` | `ON` (Linux GUI only) | Enable DBus support |

## Build Configuration Options

Compiler and build behavior options:

| Option | Default | Description |
|--------|---------|-------------|
| `ENABLE_HARDENING` | `ON` | Attempt to harden executables |
| `REDUCE_EXPORTS` | `OFF` | Reduce exported symbols |
| `WERROR` | `OFF` | Treat compiler warnings as errors |
| `WITH_CCACHE` | `ON` | Use ccache for compilation |
| `INSTALL_MAN` | `ON` | Install man pages |

## CMake Standard Options

Standard CMake options that affect the build:

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | `RelWithDebInfo` | Build type (Debug, Release, RelWithDebInfo, MinSizeRel) |
| `CMAKE_INSTALL_PREFIX` | `/usr/local` | Installation directory prefix |
| `CMAKE_C_COMPILER` | (auto-detected) | C compiler to use |
| `CMAKE_CXX_COMPILER` | (auto-detected) | C++ compiler to use |
| `CMAKE_TOOLCHAIN_FILE` | (none) | CMake toolchain file for cross-compilation |

## CMake Path Options

Options to specify custom library and include paths:

### Boost
| Option | Description |
|--------|-------------|
| `Boost_ROOT` | Root directory of Boost installation |
| `Boost_INCLUDEDIR` | Directory containing Boost headers |
| `Boost_LIBRARYDIR` | Directory containing Boost libraries |

### Berkeley DB
| Option | Description |
|--------|-------------|
| `BerkeleyDB_ROOT` | Root directory of Berkeley DB installation |
| `BerkeleyDB_INCLUDE_DIR` | Directory containing Berkeley DB headers |
| `BerkeleyDB_LIBRARY` | Path to Berkeley DB library |

### libevent
| Option | Description |
|--------|-------------|
| `Libevent_ROOT` | Root directory of libevent installation |
| `Libevent_INCLUDE_DIR` | Directory containing libevent headers |
| `Libevent_LIBRARY` | Path to libevent library |

### ZeroMQ
| Option | Description |
|--------|-------------|
| `ZeroMQ_ROOT` | Root directory of ZeroMQ installation |
| `ZeroMQ_INCLUDE_DIR` | Directory containing ZeroMQ headers |
| `ZeroMQ_LIBRARY` | Path to ZeroMQ library |

### Qt (for GUI builds)
| Option | Description |
|--------|-------------|
| `Qt5_DIR` | Directory containing Qt5Config.cmake |
| `QT_QMAKE_EXECUTABLE` | Path to qmake executable |

### Other Libraries
| Option | Description |
|--------|-------------|
| `PkgConfig_EXECUTABLE` | Path to pkg-config executable |
| `QRencode_ROOT` | Root directory of QRencode installation |
| `MiniUPnPc_ROOT` | Root directory of MiniUPnPc installation |

## Advanced Options

Less commonly used options:

| Option | Default | Description |
|--------|---------|-------------|
| `APPEND_CPPFLAGS` | (empty) | Additional preprocessor flags |
| `BUILD_SHARED_LIBS` | `OFF` | Build shared libraries instead of static |

## Example Configurations

### Minimal Build
Build only the daemon without wallet or GUI:
```bash
cmake -S . -B build \
  -DBUILD_GUI=OFF \
  -DENABLE_WALLET=OFF \
  -DBUILD_CLI=OFF \
  -DBUILD_GENESIS=OFF \
  -DENABLE_TESTS=OFF \
  -DENABLE_BENCH=OFF
```

### Full GUI Build
Build with all features including GUI:
```bash
cmake -S . -B build \
  -DBUILD_GUI=ON \
  -DENABLE_WALLET=ON \
  -DENABLE_ZMQ=ON \
  -DWITH_QRENCODE=ON \
  -DENABLE_TESTS=ON
```

### Development Build
Build for development with debugging and all tests:
```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_TESTS=ON \
  -DENABLE_BENCH=ON \
  -DWERROR=ON \
  -DENABLE_TRACING=ON
```

### Cross-compilation with depends
Use the depends system for cross-compilation:
```bash
# First build dependencies
cd depends
make HOST=x86_64-linux-gnu
cd ..

# Then configure with toolchain
cmake -S . -B build --toolchain depends/x86_64-linux-gnu/toolchain.cmake
cmake --build build
```

### Custom Library Paths
Specify custom paths for dependencies:
```bash
cmake -S . -B build \
  -DBoost_ROOT=/opt/boost \
  -DBerkeleyDB_ROOT=/opt/db4 \
  -DLibevent_ROOT=/opt/libevent \
  -DZeroMQ_ROOT=/opt/zeromq
```

## Integration with Depends System

The [depends system](../depends/README.md) automatically generates CMake toolchain files that configure all necessary paths and options. When using depends, most path options are set automatically:

```bash
# Build dependencies first. Supported hosts are 
#  x86_64-linux-gnu 
#  aarch64-linux-gnu 
#  x86_64-apple-darwin
#  x86_64-apple-darwin
make -C depends HOST=x86_64-linux-gnu

# Use generated toolchain
cmake -S . -B build --toolchain depends/x86_64-linux-gnu/toolchain.cmake
cmake --build build
```

The depends system also supports these build options through environment variables:
- `NO_QT=1` → `BUILD_GUI=OFF`
- `NO_WALLET=1` → `ENABLE_WALLET=OFF`
- `NO_UPNP=1` → UPnP libraries not linked
- `NO_USDT=1` → `ENABLE_TRACING=OFF`

## Checking Current Configuration

To see all configured options and their values:
```bash
cmake -S . -B build -LAH
```

To see only the options you can change:
```bash
cmake -S . -B build -L
```