UNIX BUILD NOTES
====================
Some notes on how to build Tapyrus Core in Unix using CMake.

Note
---------------------
Tapyrus Core uses CMake as its build system. Always use absolute paths when specifying
dependency locations. The depends system automatically handles most dependencies.

To Build
---------------------

```bash
# Build dependencies first (recommended)
cd depends
make
cd ..

# Configure and build with CMake
cmake -S . -B build
cmake --build build
cmake --install build  # optional
```

Alternative build without depends:
```bash
cmake -S . -B build
cmake --build build
```


Dependencies
---------------------

For a complete overview and versions used, see [dependencies.md](dependencies.md)

Memory Requirements
--------------------

C++ compilers are memory-hungry. It is recommended to have at least 1.5 GB of
memory available when compiling Tapyrus Core. On systems with less, you can:

Limit parallel jobs:
```bash
cmake --build build --parallel 1
```

Use clang (often less resource hungry) instead of gcc:
```bash
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
```

Build in Release mode to reduce memory usage:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

CMake Build Options
-------------------

Tapyrus Core's CMake build system supports many configuration options. For a comprehensive list of all available options, including path settings and advanced configuration, see [build-cmake.md](build-cmake.md).

Common examples:
```bash
# Basic build
cmake -S . -B build
cmake --build build

# Disable GUI and wallet
cmake -S . -B build -DBUILD_GUI=OFF -DENABLE_WALLET=OFF

# Cross-compilation with depends
cmake -S . -B build --toolchain depends/x86_64-linux-gnu/toolchain.cmake
```

## Linux Distribution Specific Instructions

### Ubuntu & Debian

#### Dependency Build Instructions

Build requirements:

    sudo apt-get install build-essential cmake pkg-config python3

Now, you can either build from self-compiled [depends](/depends/README.md) or install the required dependencies:

    sudo apt-get install libevent-dev libboost-dev

BerkeleyDB is required for the wallet.

**For Ubuntu only:** db4.8 packages are available [here](https://launchpad.net/~bitcoin/+archive/bitcoin).
You can add the repository and install using the following commands:

    sudo apt-get install software-properties-common
    sudo add-apt-repository ppa:bitcoin/bitcoin
    sudo apt-get update
    sudo apt-get install libdb4.8-dev libdb4.8++-dev

Ubuntu and Debian have their own libdb-dev and libdb++-dev packages, but these will install
BerkeleyDB 5.1 or later, which break binary wallet compatibility with the distributed executables which
are based on BerkeleyDB 4.8. If you do not care about wallet compatibility,
use `-DWITH_INCOMPATIBLE_BDB=ON` when configuring CMake.

See the section "Disable-wallet mode" to build Tapyrus Core without wallet.

Optional (see --with-miniupnpc and --enable-upnp-default):

    sudo apt-get install libminiupnpc-dev

ZMQ dependencies (provides ZMQ API 4.x):

    sudo apt-get install zeromq-devel

User-Space, Statically Defined Tracing (USDT) dependencies:

    sudo apt install systemtap-sdt-dev

GUI dependencies:

If you want to build tapyrus-qt, make sure that the required packages for Qt development
are installed. Qt6 is necessary to build the GUI.
To build without GUI use `-DBUILD_GUI=OFF`.

To build with Qt6 you need the following:

    sudo apt-get install qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools

libqrencode (optional) can be installed with:

    sudo apt-get install libqrencode-dev

Once these are installed, they will be found by CMake and a tapyrus-qt executable will be
built by default.

### Fedora

#### Dependency Build Instructions

Build requirements:

    sudo dnf install gcc-c++ make cmake python3

Optional:

    sudo dnf install libevent-devel boost-devel

Berkeley DB is required for the legacy wallet:

    sudo dnf install libdb4-devel libdb4-cxx-devel

User-Space, Statically Defined Tracing (USDT) dependencies:

    sudo dnf install systemtap-sdt-devel

GUI dependencies:

If you want to build tapyrus-qt, make sure that the required packages for Qt development are installed. Qt 6 is necessary to build the GUI. To build without GUI use `-DBUILD_GUI=OFF`.

To build with Qt 6 you need the following:

    sudo dnf install qt6-qtbase-devel qt6-qttools-devel

Additionally, to support Wayland protocol for modern desktop environments:

    sudo dnf install qt6-qtwayland

libqrencode (optional) can be installed with:

    sudo dnf install qrencode-devel

Once these are installed, they will be found by CMake and a tapyrus-qt executable will be built by default.


Notes
-----
The release is built with GCC and then "strip tapyrusd" to strip the debug
symbols, which reduces the executable size by about 90%.


miniupnpc
---------

[miniupnpc](http://miniupnp.free.fr/) may be used for UPnP port mapping.  It can be downloaded from [here](
http://miniupnp.tuxfamily.org/files/).  UPnP support is compiled in and
turned off by default.  See the CMake options for upnp behavior desired:

	-DWITH_MINIUPNPC=OFF         No UPnP support, miniupnp not required
	-DENABLE_UPNP_DEFAULT=OFF    (the default) UPnP support turned off by default at runtime
	-DENABLE_UPNP_DEFAULT=ON     UPnP support turned on by default at runtime


Berkeley DB
-----------
It is recommended to use Berkeley DB 4.8. If you have to build it yourself,
you can use [the installation script included in contrib/](/contrib/install_db4.sh)
like so

```shell
./contrib/install_db4.sh `pwd`
```

from the root of the repository.

**Note**: You only need Berkeley DB if the wallet is enabled (see the section *Disable-Wallet mode* below).

Boost
-----
If you need to build Boost yourself:

	sudo su
	./bootstrap.sh
	./bjam install


Security
--------
To help make your Tapyrus Core installation more secure by making certain attacks impossible to
exploit even if a vulnerability is found, binaries are hardened by default.
This can be controlled with:

Hardening Flags:

	cmake -S . -B build -DENABLE_HARDENING=ON   # default
	cmake -S . -B build -DENABLE_HARDENING=OFF  # disable hardening


Hardening enables the following features:

* Position Independent Executable
    Build position independent code to take advantage of Address Space Layout Randomization
    offered by some kernels. Attackers who can cause execution of code at an arbitrary memory
    location are thwarted if they don't know where anything useful is located.
    The stack and heap are randomly located by default, but this allows the code section to be
    randomly located as well.

    On an AMD64 processor where a library was not compiled with -fPIC, this will cause an error
    such as: "relocation R_X86_64_32 against `......' can not be used when making a shared object;"

    To test that you have built PIE executable, install scanelf, part of paxutils, and use:

       scanelf -e ./tapyrus

    The output should contain:

     TYPE
    ET_DYN

* Non-executable Stack
    If the stack is executable then trivial stack-based buffer overflow exploits are possible if
    vulnerable buffers are found. By default, Tapyrus Core should be built with a non-executable stack,
    but if one of the libraries it uses asks for an executable stack or someone makes a mistake
    and uses a compiler extension which requires an executable stack, it will silently build an
    executable without the non-executable stack protection.

    To verify that the stack is non-executable after compiling use:
    `scanelf -e ./tapyrus`

    The output should contain:
	STK/REL/PTL
	RW- R-- RW-

    The STK RW- means that the stack is readable and writeable but not executable.

Disable-wallet mode
--------------------
When the intention is to run only a P2P node without a wallet, Tapyrus Core may be compiled in
disable-wallet mode with:

    cmake -S . -B build -DENABLE_WALLET=OFF

In this case there is no dependency on Berkeley DB 4.8.

Mining is also possible in disable-wallet mode, but only using the `getblocktemplate` RPC
call not `getwork`.

Additional CMake Options
------------------------
A list of additional CMake options can be displayed with:

    cmake -S . -B build -LH


Setup and Build Example: Arch Linux
-----------------------------------
This example lists the steps necessary to setup and build a command line only, non-wallet distribution of the latest changes on Arch Linux:

    pacman --sync --needed boost cmake gcc git libevent make pkgconf python3
    git clone https://github.com/chaintope/tapyrus-core.git
    cd tapyrus-core/
    cmake -S . -B build -DBUILD_GUI=OFF -DENABLE_WALLET=OFF
    cmake --build build
    ctest --test-dir build

Note:
Enabling wallet support requires either compiling against a Berkeley DB newer than 4.8 (package `db`) using `-DWITH_INCOMPATIBLE_BDB=ON`,
or building and depending on a local version of Berkeley DB 4.8. The readily available Arch Linux packages are currently built using
`-DWITH_INCOMPATIBLE_BDB=ON` according to the [PKGBUILD](https://projects.archlinux.org/svntogit/community.git/tree/tapyrus/trunk/PKGBUILD).
As mentioned above, when maintaining portability of the wallet between the standard Tapyrus Core distributions and independently built
node software is desired, Berkeley DB 4.8 must be used.



For further documentation on the depends system see [README.md](../depends/README.md) in the depends directory.

