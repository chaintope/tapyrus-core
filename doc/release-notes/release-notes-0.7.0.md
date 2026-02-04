Tapyrus version 0.7.0 is now available for download at:
<https://github.com/chaintope/tapyrus-core/releases/tag/v0.7.0>

Please report bugs using the issue tracker at github:
<https://github.com/chaintope/tapyrus-core/issues>

Project source code is hosted at github; you can get
source-only tarballs/zipballs directly from there:
<https://github.com/chaintope/tapyrus-core/tarball/v0.7.0>

How to Upgrade
==============

It is recommended to upgrade all older nodes to the latest release as it contains fixes to vulnerabilities. If you are running a node on
tapyrus testnet follow the instruction in [getting_started](/doc/tapyrus/getting_started.md#how-to-start-a-node-on-tapyrus-testnet) to start a new Tapyrus v0.7.0 node.

If you are running a private tapyrus network using older versions of tapyrus-core release, shut it down. Wait until it has completely shut down.
Follow the instruction in [getting_started](/doc/tapyrus/getting_started.md#how-to-start-a-new-tapyrus-network) to start a new Tapyrus v0.7.0 network. Tapyrus blockchain created by older versions, before v0.5.0
are not compatible with v0.7.0 because of the absence of xfield in the block header. But blockchain created by v0.5.0, v0.5.1, v0.5.2 and v0.6.1 is compatible with v0.7.0.

Downgrading warning
-------------------

If you upgrade to a node running v0.7.0 and need to switch back to v0.6.1, there is no issue. These two versions are compatible. But if downgrading to versions v0.5.*, the blockchain and chainstate on the node are backward compatible as long as **no xfield change** was mode in the network.

Compatibility
-------------

Tapyrus v0.7.0 is supported on Linux, Macos and Windows platform in their supported CPU architectures namely x86_x64 and arm64(aarch64)

Change Summary
==============

This release is an upgrade of all tapyrus dependencies and tools along with some bug fixes.

- Tapyrus v0.7.0 codebase supports C++20 standard.
- Cmake build is adapted for tapyrus and all supported dependencies in depends directory. Autotools has been removed completely.
- Qt framerwork in Tapyrus v0.7.0 is Qt6, built using Cmake and Ninja.
- Docker containers *tapyrusd* and *tapyrus-builder* use Cmake.
- Guix build is used for generating the release distributions.

0.7.0 change log
================

*Improvements:*

- [PR331](https://github.com/chaintope/tapyrus-core/pull/331), [PR335](https://github.com/chaintope/tapyrus-core/pull/335), [PR336](https://github.com/chaintope/tapyrus-core/pull/336), [PR337](https://github.com/chaintope/tapyrus-core/pull/337) Cmake build system for linux and Macos
- [PR332](https://github.com/chaintope/tapyrus-core/pull/332) Upgrade MACos SDK and remove Boost filesystem from tapyrus builds
- [PR346](https://github.com/chaintope/tapyrus-core/pull/346) Tapyrus containers with Cmake support
- [PR358](https://github.com/chaintope/tapyrus-core/pull/358) Support C++20 compilation using cmake
- [PR362](https://github.com/chaintope/tapyrus-core/pull/362) Support Windows Cmake x86_64 build
- [PR366](https://github.com/chaintope/tapyrus-core/pull/366) Port GUIX build system from Bitcoin
- [PR333](https://github.com/chaintope/tapyrus-core/pull/333) Support MacOS cross compilation with Qt5 upgrade to 5.15.16 and zeromq upgrade
- [PR368](https://github.com/chaintope/tapyrus-core/pull/368), [PR381](https://github.com/chaintope/tapyrus-core/pull/381) Upgrade tapyrus-qt from Qt5 to Qt6
- [PR359](https://github.com/chaintope/tapyrus-core/pull/359) Port prevector class from bitcoin core

*Fix vulnerabilities inherited from bitcoin core:*

- [PR353](https://github.com/chaintope/tapyrus-core/pull/353) Port fix CVE-2024-52919 - Remote crash due to addr message spam
- [PR356](https://github.com/chaintope/tapyrus-core/pull/356) Port Bitcoin PR 24858 which fixes the incorrect block file size calculation in reindex

*Bug fixes:*

- [PR328](https://github.com/chaintope/tapyrus-core/pull/328) Fix fee output in get transaction rpc output
- [PR357](https://github.com/chaintope/tapyrus-core/pull/357) Fix reindexing failure when XField Max Block size is changed in the network
- [PR349](https://github.com/chaintope/tapyrus-core/pull/349) Fix check queue deadlock where master thread does not realize that worker threads have finished work
- [PR363](https://github.com/chaintope/tapyrus-core/pull/363) Make the time conversion timezone aware in import wallet
- [PR365](https://github.com/chaintope/tapyrus-core/pull/365) Remove all usages of locale dependant function in tapyrus codebase
- [PR369](https://github.com/chaintope/tapyrus-core/pull/369) Fix crash introduced while porting the fix for getblocktxn vulnerability
- [PR341](https://github.com/chaintope/tapyrus-core/pull/341) Fix lock directory test and thread safety warnings
- [PR340](https://github.com/chaintope/tapyrus-core/pull/340) Fix cmake compilation warnings
- [PR338](https://github.com/chaintope/tapyrus-core/pull/338) Fix Cmake unit tests failures which were not identified by autotools

*Improvements in testing:*

- [PR342](https://github.com/chaintope/tapyrus-core/pull/342) Cmake CI with scheduled Daily test and a lighter Smoke test for every commit
- [PR334](https://github.com/chaintope/tapyrus-core/pull/334) Standardize all timeout intervals in functional tests
- [PR375](https://github.com/chaintope/tapyrus-core/pull/375) Fix race condition in only token filter test
- [PR372](https://github.com/chaintope/tapyrus-core/pull/372) Add comprehensive edge case tests for CChainState, CChain and CBlockIndex classes
- [PR374](https://github.com/chaintope/tapyrus-core/pull/374) Add ConnectionResetError to the errors identified and handled by peer connection code in the test framework
