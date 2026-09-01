Dependencies
============

These are the dependencies currently used by Tapyrus Core. You can find instructions for installing them in the `build-*.md` file for your platform.

|Dependency | Current version (CI) | Minimum required |
|----|----|----|
|CMake|3.28|3.22|
|Clang|18.1.3|18.1.3|
|GCC|13.2.0|13.2.0|
|Python (scripts, tests)|3.10|3.10|

`Current version (CI)` is what the Linux CI jobs actually install (`apt-get install -y cmake`/`clang`/`gcc g++`, unpinned -- whatever Ubuntu's own repos currently resolve to; none of these are pinned to an exact version or printed via `--version` anywhere in the workflow files, so this column reflects the versions on the runner image at the time this table was last checked, not something re-derivable from the repo alone). `Minimum required` is the actual floor this repo's own build enforces: `CMakeLists.txt:11`'s `cmake_minimum_required(VERSION 3.22)` for CMake, and `find_package(Python3 3.10 ...)` (`CMakeLists.txt:542`) for Python. Neither compiler has a version check anywhere in `CMakeLists.txt`/`cmake/module/*.cmake` -- only `CMAKE_CXX_COMPILER_ID` is checked (for MSVC branching), never `CMAKE_CXX_COMPILER_VERSION` against a floor -- so Clang/GCC's `Minimum required` is just the current version repeated, same as most rows in the table below.

`Minimum required` in the table below is the actual floor enforced by this repo's own `CMakeLists.txt`/`cmake/module/*.cmake` (via `find_package(... <version> ...)`) where one exists. Most of these packages aren't `find_package()`d by this repo at all -- they're Qt's own transitive build dependencies for its static xcb platform plugin, or build-time tools with no version check -- so for those, `Minimum required` is just the pinned `Version` column; there's no independently-enforced floor to report.

`Platform` records when a package is only built for some configurations, per `depends/packages/packages.mk` and the `qt_packages_$(NO_QT)` gating in `depends/Makefile`: `qt_packages` (qrencode) and `qt` itself (built per host via `qt_linux_packages`/`qt_darwin_packages`/`qt_mingw32_packages`) are GUI only -- dropped entirely under `NO_QT=1`, regardless of host OS. The rest of `qt_linux_packages` -- Qt's static xcb platform plugin and its transitive deps (expat, freetype, fontconfig, libXau, xproto, xcb_proto, libxkbcommon, and the libxcb family) -- is Linux + GUI only: never built for macOS/Windows Qt, and also dropped under `NO_QT=1` on Linux. `bdb` only builds when the wallet is enabled (`wallet_packages`), `miniupnpc` only when UPnP is enabled (`upnp_packages`), and `systemtap` only when USDT tracing is enabled *and* the host is Linux (`usdt_linux_packages`). A blank cell means the package is built the same way regardless of platform/feature flags.

| Package | Platform | Minimum required | Version | File name | SHA256 | Download URL |
| --- | --- | --- | --- | --- | --- | --- |
| Berkeley DB | Wallet only | 4.8 | 4.8.30 | `db-4.8.30.NC.tar.gz` | `12edc0df75bf9abd7f82f821795bcee50f42cb2e5f76a6a281b85732798364ef` | [download.oracle.com](https://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz) |
| Boost | | 1.73.0 | 1.81.0 | `boost_1_81_0.tar.gz` | `205666dea9f6a7cfed87c7a6dfbeb52a2c1b9de55712c9c1a87735d7181452b6` | [archives.boost.io](https://archives.boost.io/release/1.81.0/source/boost_1_81_0.tar.gz) |
| Expat | Linux + GUI only | 2.8.3 | 2.8.3 | `expat-2.8.3.tar.xz` | `f6256df90c906773d344da084402b7d3e4f22ed41b1a59c989098a83d3ea0c85` | [github.com](https://github.com/libexpat/libexpat/releases/download/R_2_8_3/expat-2.8.3.tar.xz) |
| fontconfig | Linux + GUI only | 2.16.0 | 2.16.0 | `fontconfig-2.16.0.tar.xz` | `6a33dc555cc9ba8b10caf7695878ef134eeb36d0af366041f639b1da9b6ed220` | [freedesktop.org](https://www.freedesktop.org/software/fontconfig/release/fontconfig-2.16.0.tar.xz) |
| FreeType | Linux + GUI only | 2.11.0 | 2.11.0 | `freetype-2.11.0.tar.xz` | `8bee39bd3968c4804b70614a0a3ad597299ad0e824bc8aad5ce8aaf48067bde7` | [savannah.gnu.org](https://download.savannah.gnu.org/releases/freetype/freetype-2.11.0.tar.xz) |
| libevent | | 2.1.8 | 2.1.12-stable | `libevent-2.1.12-stable.tar.gz` | `92e6de1be9ec176428fd2367677e61ceffc2ee1cb119035037a27d346b0403bb` | [github.com](https://github.com/libevent/libevent/releases/download/release-2.1.12-stable/libevent-2.1.12-stable.tar.gz) |
| libXau | Linux + GUI only | 1.0.12 | 1.0.12 | `libXau-1.0.12.tar.gz` | `2402dd938da4d0a332349ab3d3586606175e19cb32cb9fe013c19f1dc922dcee` | [xorg.freedesktop.org](https://xorg.freedesktop.org/releases/individual/lib/libXau-1.0.12.tar.gz) |
| MiniUPnPc | UPnP only | 2.3.3 | 2.3.3 | `miniupnpc-2.3.3.tar.gz` | `d52a0afa614ad6c088cc9ddff1ae7d29c8c595ac5fdd321170a05f41e634bd1a` | [github.com](https://github.com/miniupnp/miniupnp/releases/download/miniupnpc_2_3_3/miniupnpc-2.3.3.tar.gz) |
| qrencode | GUI only | 4.1.1 | 4.1.1 | `qrencode-4.1.1.tar.bz2` | `e455d9732f8041cf5b9c388e345a641fd15707860f928e94507b1961256a6923` | [fukuchi.org](https://fukuchi.org/works/qrencode/qrencode-4.1.1.tar.bz2) |
| Qt (qtbase) | GUI only | 6.0 | 6.10.1 | `qtbase-everywhere-src-6.10.1.tar.xz` | `5a6226f7e23db51fdc3223121eba53f3f5447cf0cc4d6cb82a3a2df7a65d265d` | [download.qt.io](https://download.qt.io/official_releases/qt/6.10/6.10.1/submodules/qtbase-everywhere-src-6.10.1.tar.xz) |
| Qt (qttranslations) | GUI only | 6.0 | 6.10.1 | `qttranslations-everywhere-src-6.10.1.tar.xz` | `8e49a2df88a12c376a479ae7bd272a91cf57ebb4e7c0cf7341b3565df99d2314` | [download.qt.io](https://download.qt.io/official_releases/qt/6.10/6.10.1/submodules/qttranslations-everywhere-src-6.10.1.tar.xz) |
| Qt (qttools) | GUI only | 6.0 | 6.10.1 | `qttools-everywhere-src-6.10.1.tar.xz` | `8148408380ffea03101a26305c812b612ea30dbc07121e58707601522404d49b` | [download.qt.io](https://download.qt.io/official_releases/qt/6.10/6.10.1/submodules/qttools-everywhere-src-6.10.1.tar.xz) |
| systemtap | USDT + Linux only | 5.5 | 5.5 | `systemtap-5.5.tar.gz` | `980e58887a284097b9d4c6ae6382b75787573131c27e3875c0fc94bceb8c61a8` | [sourceware.org](https://sourceware.org/systemtap/ftp/releases/systemtap-5.5.tar.gz) |
| libxcb | Linux + GUI only | 1.17.0 | 1.17.0 | `libxcb-1.17.0.tar.gz` | `2c69287424c9e2128cb47ffe92171e10417041ec2963bceafb65cb3fcf8f0b85` | [xcb.freedesktop.org](https://xcb.freedesktop.org/dist/libxcb-1.17.0.tar.gz) |
| libxcb_util_cursor | Linux + GUI only | 0.1.6 | 0.1.6 | `xcb-util-cursor-0.1.6.tar.gz` | `eae38b2dfc5c529a886e507ef576b12d2a20aa1f149608e4853af760f31be60b` | [xcb.freedesktop.org](https://xcb.freedesktop.org/dist/xcb-util-cursor-0.1.6.tar.gz) |
| libxcb_util_image | Linux + GUI only | 0.4.1 | 0.4.1 | `xcb-util-image-0.4.1.tar.gz` | `0ebd4cf809043fdeb4f980d58cdcf2b527035018924f8c14da76d1c81001293b` | [xcb.freedesktop.org](https://xcb.freedesktop.org/dist/xcb-util-image-0.4.1.tar.gz) |
| libxcb_util_keysyms | Linux + GUI only | 0.4.1 | 0.4.1 | `xcb-util-keysyms-0.4.1.tar.gz` | `1fa21c0cea3060caee7612b6577c1730da470b88cbdf846fa4e3e0ff78948e54` | [xcb.freedesktop.org](https://xcb.freedesktop.org/dist/xcb-util-keysyms-0.4.1.tar.gz) |
| libxcb_util_render | Linux + GUI only | 0.3.10 | 0.3.10 | `xcb-util-renderutil-0.3.10.tar.gz` | `e04143c48e1644c5e074243fa293d88f99005b3c50d1d54358954404e635128a` | [xcb.freedesktop.org](https://xcb.freedesktop.org/dist/xcb-util-renderutil-0.3.10.tar.gz) |
| libxcb_util_wm | Linux + GUI only | 0.4.2 | 0.4.2 | `xcb-util-wm-0.4.2.tar.gz` | `dcecaaa535802fd57c84cceeff50c64efe7f2326bf752e16d2b77945649c8cd7` | [xcb.freedesktop.org](https://xcb.freedesktop.org/dist/xcb-util-wm-0.4.2.tar.gz) |
| libxcb_util | Linux + GUI only | 0.4.1 | 0.4.1 | `xcb-util-0.4.1.tar.gz` | `21c6e720162858f15fe686cef833cf96a3e2a79875f84007d76f6d00417f593a` | [xcb.freedesktop.org](https://xcb.freedesktop.org/dist/xcb-util-0.4.1.tar.gz) |
| xkbcommon | Linux + GUI only | 1.13.2 | 1.13.2 | `libxkbcommon-1.13.2.tar.gz` | `acc4d5f7c3cbba5f9f8d08d8bdbeede84ecede46792f47929aa9321873385528` | [github.com](https://github.com/xkbcommon/libxkbcommon/archive/refs/tags/xkbcommon-1.13.2.tar.gz) |
| xcb_proto | Linux + GUI only | 1.17.0 | 1.17.0 | `xcb-proto-1.17.0.tar.gz` | `392d3c9690f8c8202a68fdb89c16fd55159ab8d65000a6da213f4a1576e97a16` | [xorg.freedesktop.org](https://xorg.freedesktop.org/archive/individual/proto/xcb-proto-1.17.0.tar.gz) |
| xproto | Linux + GUI only | 7.0.31 | 7.0.31 | `xproto-7.0.31.tar.gz` | `6d755eaae27b45c5cc75529a12855fed5de5969b367ed05003944cf901ed43c7` | [xorg.freedesktop.org](https://xorg.freedesktop.org/releases/individual/proto/xproto-7.0.31.tar.gz) |
| ZeroMQ | | 4.3.5 | 4.3.5 | `zeromq-4.3.5.tar.gz` | `6653ef5910f17954861fe72332e68b03ca6e4d9c7160eb3a8de5a5a913bfab43` | [github.com](https://github.com/zeromq/libzmq/releases/download/v4.3.5/zeromq-4.3.5.tar.gz) |
| macOS SDK | macOS only | Xcode 26.2 (17C52) | Xcode 26.2 (17C52) | `Xcode-26.2-17C52-extracted-SDK-with-libcxx-headers.tar.gz` | *(not checked; see below)* | [bitcoincore.org](https://bitcoincore.org/depends-sources/sdks/Xcode-26.2-17C52-extracted-SDK-with-libcxx-headers.tar.gz) |

Package/version/hash/URL data above is copied from `depends/packages/*.mk` and `depends/hosts/darwin.mk` for convenience — it will drift if those files change without this table being updated too. Treat the `.mk` files as the source of truth.

Building Qt also requires three small CMake scaffolding files (`depends/packages/qt_details.mk`) fetched directly from the `qt/qt5` GitHub repository rather than a release archive. They're part of that repo, not a standalone release artifact, so they aren't listed in the table above and don't need S3 mirroring.

## S3 fallback mirror

`depends` downloads each package above from its "Download URL". If that primary host is unreachable, it automatically falls back to `FALLBACK_DOWNLOAD_PATH`, an S3 mirror at [https://s3.ap-northeast-1.amazonaws.com/repo.tapyrus.chaintope.com](https://s3.ap-northeast-1.amazonaws.com/repo.tapyrus.chaintope.com) (see `depends/README.md`'s "Dependency Options" section for how `FALLBACK_DOWNLOAD_PATH` works generally, and `depends/Makefile` for this repo's default value). The mirror is flat (no subdirectories) and must serve each file under the exact "File name" listed in the table above.

The macOS SDK has its own separate fallback wired up directly in `.github/workflows/daily-test.yml` (not through `depends`' `FALLBACK_DOWNLOAD_PATH`), pointing at the same bucket. Neither the primary nor fallback SDK download is hash-checked anywhere in that workflow.

`libxkbcommon` >= 1.9.0 (including the 1.13.2 pinned above) requires Meson >= 1.4.0, newer than the `meson` package Ubuntu 24.04 ships via `apt`. CI installs a pinned `meson==1.4.0` via pip rather than relying on the distro package. It also needs `gperf`, required by `fontconfig`'s build. Building the GUI through `depends` on Linux needs both — see `depends/README.md`.

`libxkbcommon`'s Download URL above points at a GitHub tag archive, not `xkbcommon.org`: upstream stopped publishing releases there after 1.7.0 (every later version 404s), and only publishes via GitHub tags from 1.8.0 onward, including the 1.13.2 pinned above.
