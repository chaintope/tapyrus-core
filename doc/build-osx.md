macOS Build Instructions and Notes
====================================
The commands in this guide should be executed in a Terminal application.
The built-in one is located in `/Applications/Utilities/Terminal.app`.

Preparation
-----------
Install the macOS command line tools:

`xcode-select --install`

When the popup appears, click `Install`.

To install the Homebrew package manager, see: https://brew.sh

Dependencies
----------------------

To build tapyrus-core without GUI install only these dependencies:

    brew install cmake berkeley-db4 boost miniupnpc pkg-config python libevent zeromq

GUI Dependencies
---------------------

To build tapyrus-qt GUI install the following along with the above:

    brew install qt6 qrencode

If you want to build the disk image with `cmake --build build --target deploy`, you will need the following additional dependencies:

    brew install zip

See [dependencies.md](dependencies.md) for a complete overview.

Berkeley DB
-----------
It is recommended to use Berkeley DB 4.8. If you have to build it yourself,
you can use [the installation script included in contrib/](/contrib/install_db4.sh)
like so

```shell
./contrib/install_db4.sh .
```

from the root of the repository.

**Note**: You only need Berkeley DB if the wallet is enabled (see the section *Disable-Wallet mode* below).

Build Tapyrus Core
------------------------

1. Clone the Tapyrus Core source code and cd into `tapyrus-core`

        git clone --recursive https://github.com/chaintope/tapyrus-core
        cd tapyrus-core

2.  Build Tapyrus Core:

    Configure and build the headless Tapyrus Core binaries.

        cmake -S . -B build -DBUILD_GUI=OFF
        cmake --build build

3.  It is recommended to build and run the unit tests:

        ctest --test-dir build

4.  You can also create a .dmg that contains the .app bundle (optional):

        cmake --build build --target deploy

Build Output
------------

The following are built by Tapyrus Core:

    ./build/bin/tapyrusd # Starts the tapyrus daemon.
    ./build/bin/tapyrus-cli --help # Outputs a list of command-line options.
    ./build/bin/tapyrus-cli help # Outputs a list of RPC commands when the daemon is running.
    ./build/bin/tapyrus-genesis # Tool to create a tapyrus genesis block.
    ./build/bin/tapyrus-tx # Tool to create a tapyrus transaction.

Refer to [FAQ](https://github.com/chaintope/tapyrus-core/new/master/doc/tapyrus#faq) section of getting started documentation for instructions on how to run tapyrus

Common CMake Options for macOS
------------------------------

To build with GUI (Qt):
```bash
cmake -S . -B build
cmake --build build
```

To build without wallet:
```bash
cmake -S . -B build -DENABLE_WALLET=OFF
cmake --build build
```

To use system Berkeley DB (not recommended):
```bash
cmake -S . -B build -DWITH_INCOMPATIBLE_BDB=ON
cmake --build build
```

To build for different architectures:
```bash
# For Intel x86_64
cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES=x86_64

# For Apple Silicon aarch64
cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES=aarch64

# Universal binary (both architectures)
cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES="x86_64;aarch64"
```

Notes
-----

* Tested on macOS 10.15 Catalina and newer on both Intel and Apple Silicon processors.
* For M1/M2 Macs, native aarch64 builds are recommended for best performance.

