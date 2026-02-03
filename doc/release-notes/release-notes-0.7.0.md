Tapyrus version 0.7.0 is now available for download at:
https://github.com/chaintope/tapyrus-core/releases/tag/v0.7.0

Please report bugs using the issue tracker at github:
https://github.com/chaintope/tapyrus-core/issues

Project source code is hosted at github; you can get
source-only tarballs/zipballs directly from there:
https://github.com/chaintope/tapyrus-core/tarball/v0.7.0


How to Upgrade
==============

It is recommended to upgrade all older nodes to the latest release as it contains fixes to vulnerabilities. If you are running a node on 
tapyrus testnet follow the instruction in [getting_started](/doc/tapyrus/getting_started.md#how-to-start-a-node-on-tapyrus-testnet) to start a new Tapyrus v0.7.0 node.

If you are running a private tapyrus network using older versions of tapyrus-core release, shut it down. Wait until it has completely shut down. 
Follow the instruction in [getting_started](/doc/tapyrus/getting_started.md#how-to-start-a-new-tapyrus-network) to start a new Tapyrus v0.7.0 network. Tapyrus blockchain created by older versions, before v0.5.0 
are not compatible with v0.7.0 because of the absence of xfield in the block header. But blockchain created by v0.5.0, v0.5.1, v0.5.2 and v0.6.1 is compatible 
with v0.7.0.

Downgrading warning
-------------------

If you upgrade to a node running v0.7.0 and need to switch back to v0.6.1, there is no issue. These two versions are compatible. But if downgrading to versions v0.5.*, the blockchain and chainstate on the node are backward compatible as long as **no xfield change** was mode in the network.

Compatibility
-------------

Tapyrus v0.7.0 is supported on Linux, Macos and Windows platform in their supported CPU architectures namely x86_x64 and arm64(aarch64)


This release is an upgrade of all tapyrus dependencies and tools to newer build systems with some bug fixes

- Cmake build is adapted for tapyrus and all supported dependencies in depends directory. Autotools has been removed completely.
- Qt framerwork in Tapyrusv0.7.0 is Qt6, built using Cmake and Ninja
- Docker containers tapyrusd and tapyrus-builder use Cmake 
- Guix build is used for generating the release distributions.

0.7.0 change log
================

*All Upgrades*

#331,#335,#336,#337 Cmake build system for linux and Macos
#332 Upgrade MACos SDK and remove Boost filesystem from tapyrus builds
#346 Tapyrus containers with Cmake support
#358 Support C++20 compilation using cmake
#362 Support Windows Cmake x86_64 build
#366 Port GUIX build system from Bitcoin
#333 Qt5 upgrade to 5.15.16 
#368, #381 Upgrade tapyrus-qt from Qt5 to Qt6

*Fix vulnerabilities inherited from bitcoin core:*

#353 Port fix CVE-2024-52919 - Remote crash due to addr message spam 
#356 Port Bitcoin PR 24858 which fixes the incorrect block file size calculation in reindex

*Bug fixes*

#375,#367, #372, #374 Improvements and bug fixes in test cases
#328 Fix fee output in get transaction rpc output 
#357 Fix reindexing failure when Xfield Max Block size is changed in the network
#349 Fix check queue deadlock where master thread does not realize that worker threads have finished work
#363 Make the time conversion timezone aware in import wallet
#365 Remove usage of locale dependant function in the complete codebase
#369 Fix crash introduced while porting the fix for getblocktxn vulnerability