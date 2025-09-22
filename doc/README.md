Tapyrus Core
=============

Tapyrus is a blockchain that has been forked from Bitcoin Core, and solves the governance issues. Bitcoin uses Proof of Work as a consensus algorithm, but Tapyrus creates a block (ie, approves a transaction) with multiple signatures from the Signer group which consists of industry stakeholders. Therefore, transactions on the blockchain are stably approved, and finality is given at the time of approval. Anyone can join the Tapyrus network, and anyone can access the public ledger and create and broadcast transactions freely.


To download Tapyrus Core, visit [tapyrus-core](https://github.com/chaintope/tapyrus-core/releases/tag/v0.5.2).

Running
---------------------
The following are some helpful notes on how to run Tapyrus Core on your native platform.

### Unix

Unpack the files into a directory and run:

- `bin/tapyrus_qt` (GUI) or
- `bin/tapyrusd` (headless)

### Windows

Unpack the files into a directory, and then run tapyrus_qt.exe.

### macOS

Drag Tapyrus Core to your applications folder, and then run Tapyrus Core.

### Need Help?

* See the documentation on Bitcoin at [Bitcoin Wiki](https://en.bitcoin.it/wiki/Main_Page)
for help and introduction.
* See the documentation on Tapyrus-core at https://github.com/chaintope/tapyrus-core#readme 


Building
---------------------
The following are developer notes on how to build Tapyrus Core on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [Dependencies](dependencies.md)
- [CMake Build Options](build-cmake.md)
- [macOS Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)

Development
---------------------
The Bitcoin repo's [root README](/README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](developer-notes.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- [Source Code Documentation (External Link)](https://dev.visucore.com/bitcoin/doxygen/)
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPS](bips.md)
- [Dnsseed Policy](dnsseed-policy.md)
- [Benchmarking](benchmarking.md)


### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [Files](files.md)
- [Fuzz-testing](fuzzing.md)
- [Reduce Traffic](reduce-traffic.md)
- [Tor Support](tor.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [ZMQ](zmq.md)

License
---------------------
Distributed under the [MIT software license](/COPYING).
