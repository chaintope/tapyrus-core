macOS Build Instructions and Notes
====================================
The commands in this guide should be executed in a Terminal application.
The built-in one is located in `/Applications/Utilities/Terminal.app`.

Preparation
-----------
Install the macOS command line tools:

`xcode-select --install`

When the popup appears, click `Install`.

Then install [Homebrew](https://brew.sh).

Dependencies
----------------------

    brew install automake berkeley-db4 libtool boost miniupnpc openssl pkg-config python qt libevent qrencode

See [dependencies.md](dependencies.md) for a complete overview.

If you want to build the disk image with `make deploy` (.dmg / optional), you need RSVG

    brew install librsvg

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

1. Clone the Tapyrus Core source code and cd into `tapyrus`

        git clone https://github.com/chaintope/tapyrus-core
        cd tapyrus

2.  Build Tapyrus Core:

    Configure and build the headless Tapyrus Core binaries.

    It is recommanded to diable the GUI build by passing `--without-gui` to configure.

        ./autogen.sh
        ./configure --without-gui
        make

3.  It is recommended to build and run the unit tests:

        make check

4.  You can also create a .dmg that contains the .app bundle (optional):

        make deploy

Build Output
------------

The following are built by Tapyrus Core:

    ./src/tapyrusd # Starts the tapyrus daemon.
    ./src/tapyrus-cli --help # Outputs a list of command-line options.
    ./src/tapyrus-cli help # Outputs a list of RPC commands when the daemon is running.
    ./src/tapyrus-genesis # Tool to create a tapyrus genesis block.
    ./src/tapyrus-tx # Tool to create a tapyrus transaction.

Refer to [FAQ](https://github.com/chaintope/tapyrus-core/new/master/doc/tapyrus#faq) section of getting started documentation for instructions on how to run tapyrus

Notes
-----

* Tested on OS X 10.15 Yosemite through macOS 10.15.1 Catalina on 64-bit Intel processors only.

