Dependencies
============

These are the dependencies currently used by Tapyrus Core. You can find instructions for installing them in the `build-*.md` file for your platform.

|Dependency | Current version (CI) | Minimum required |
|----|----|----|
|CMake|3.28|3.22|
|Clang|18.1.3|18.1.3|
|GCC|13.2.0|13.2.0|
|Python (scripts, tests)|3.10|3.10|

`Current version (CI)` is what the Linux CI runners currently install; these tools are not pinned. `Minimum required` is the floor the CMake configure step enforces for CMake and Python. The configure step checks no compiler version, so the compiler minimums repeat the current version.

The packages below are built by `depends`:

- `Version` is the version `depends` builds.
- `Minimum tested` is the oldest version CI builds and tests against. For Boost, Expat, FreeType, libevent and systemtap it is the version they were bumped from, older than `Version`: the `minimum` variant of the weekly `build-from-scratch` CI job swaps in that version, builds it from scratch on every platform and runs the tests on the native ones. fontconfig, libXau and libxkbcommon were bumped too but have no older tested version, because their older releases used a different archive format or download host, which the job's version-and-hash swap can't express. For every other package `Minimum tested` equals `Version`. This floor is narrower than what the CMake configure step accepts (it still accepts Boost 1.73.0 and libevent 2.1.8); those older versions are not tested.
- `Platform` says which builds include the package: "GUI only", "Wallet only" and "UPnP only" packages are built only with that feature enabled, "Linux + GUI only" packages only for Linux GUI builds, and "USDT + Linux only" only for Linux builds with USDT tracing. A blank cell means every build includes it. See "Package Toggle Options" in [depends/README.md](../depends/README.md) for the switch that drops each group.

| Package | Platform | Minimum tested | Version | File name | SHA256 | Download URL |
| --- | --- | --- | --- | --- | --- | --- |
| Berkeley DB | Wallet only | 5.3.28 | 5.3.28 | `db-5.3.28.NC.tar.gz` | `76a25560d9e52a198d37a31440fd07632b5f1f8f9f2b6d5438f4bc3e7c9013ef` | [download.oracle.com](https://download.oracle.com/berkeley-db/db-5.3.28.NC.tar.gz) |
| Boost | | 1.81.0 | 1.92.0 | `boost_1_92_0.tar.gz` | `c4a3b310ddd2472416e091067166b0713be97c63f38c212c484ada022fd296ce` | [archives.boost.io](https://archives.boost.io/release/1.92.0/source/boost_1_92_0.tar.gz) |
| Expat | Linux + GUI only | 2.4.8 | 2.8.3 | `expat-2.8.3.tar.xz` | `f6256df90c906773d344da084402b7d3e4f22ed41b1a59c989098a83d3ea0c85` | [github.com](https://github.com/libexpat/libexpat/releases/download/R_2_8_3/expat-2.8.3.tar.xz) |
| fontconfig | Linux + GUI only | 2.16.0 | 2.16.0 | `fontconfig-2.16.0.tar.xz` | `6a33dc555cc9ba8b10caf7695878ef134eeb36d0af366041f639b1da9b6ed220` | [freedesktop.org](https://www.freedesktop.org/software/fontconfig/release/fontconfig-2.16.0.tar.xz) |
| FreeType | Linux + GUI only | 2.11.0 | 2.14.3 | `freetype-2.14.3.tar.xz` | `36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f` | [savannah.gnu.org](https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.xz) |
| libevent | | 2.1.12-stable | 2.1.13-stable | `libevent-2.1.13-stable.tar.gz` | `f7e9383b8c0baa81b687e5b5eecc01beefaf1b19b64151d95ed61647fe7a315c` | [github.com](https://github.com/libevent/libevent/releases/download/release-2.1.13-stable/libevent-2.1.13-stable.tar.gz) |
| libXau | Linux + GUI only | 1.0.12 | 1.0.12 | `libXau-1.0.12.tar.gz` | `2402dd938da4d0a332349ab3d3586606175e19cb32cb9fe013c19f1dc922dcee` | [xorg.freedesktop.org](https://xorg.freedesktop.org/releases/individual/lib/libXau-1.0.12.tar.gz) |
| MiniUPnPc | UPnP only | 2.3.3 | 2.3.3 | `miniupnpc-2.3.3.tar.gz` | `d52a0afa614ad6c088cc9ddff1ae7d29c8c595ac5fdd321170a05f41e634bd1a` | [github.com](https://github.com/miniupnp/miniupnp/releases/download/miniupnpc_2_3_3/miniupnpc-2.3.3.tar.gz) |
| qrencode | GUI only | 4.1.1 | 4.1.1 | `qrencode-4.1.1.tar.bz2` | `e455d9732f8041cf5b9c388e345a641fd15707860f928e94507b1961256a6923` | [fukuchi.org](https://fukuchi.org/works/qrencode/qrencode-4.1.1.tar.bz2) |
| Qt (qtbase) | GUI only | 6.0 | 6.10.1 | `qtbase-everywhere-src-6.10.1.tar.xz` | `5a6226f7e23db51fdc3223121eba53f3f5447cf0cc4d6cb82a3a2df7a65d265d` | [download.qt.io](https://download.qt.io/official_releases/qt/6.10/6.10.1/submodules/qtbase-everywhere-src-6.10.1.tar.xz) |
| Qt (qttranslations) | GUI only | 6.0 | 6.10.1 | `qttranslations-everywhere-src-6.10.1.tar.xz` | `8e49a2df88a12c376a479ae7bd272a91cf57ebb4e7c0cf7341b3565df99d2314` | [download.qt.io](https://download.qt.io/official_releases/qt/6.10/6.10.1/submodules/qttranslations-everywhere-src-6.10.1.tar.xz) |
| Qt (qttools) | GUI only | 6.0 | 6.10.1 | `qttools-everywhere-src-6.10.1.tar.xz` | `8148408380ffea03101a26305c812b612ea30dbc07121e58707601522404d49b` | [download.qt.io](https://download.qt.io/official_releases/qt/6.10/6.10.1/submodules/qttools-everywhere-src-6.10.1.tar.xz) |
| systemtap | USDT + Linux only | 4.7 | 5.5 | `systemtap-5.5.tar.gz` | `980e58887a284097b9d4c6ae6382b75787573131c27e3875c0fc94bceb8c61a8` | [sourceware.org](https://sourceware.org/systemtap/ftp/releases/systemtap-5.5.tar.gz) |
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
| macOS SDK | macOS only | Xcode 26.2 (17C52) | Xcode 26.2 (17C52) | `Xcode-26.2-17C52-extracted-SDK-with-libcxx-headers.tar.gz` | *(not checked; see [depends/README.md](../depends/README.md#download-fallback-mirror))* | [bitcoincore.org](https://bitcoincore.org/depends-sources/sdks/Xcode-26.2-17C52-extracted-SDK-with-libcxx-headers.tar.gz) |

**Berkeley DB downgrade warning:**

| Tapyrus Core version | Berkeley DB version |
| --- | --- |
| v0.7.1 and earlier | 4.8 |
| v0.7.2 and later | 5.3 |

A wallet written by Tapyrus Core v0.7.1 or earlier can be opened and upgraded by v0.7.2 or later. This only works in that one direction, not both: once a wallet has been opened by v0.7.2 or later, it can no longer be opened by v0.7.1 or earlier -- that older version will refuse to start against it, even after a clean shutdown of the newer version. To move a wallet back to v0.7.1 or earlier after it has been opened by a newer version, back up the wallet directory first, then either remove its `database` subdirectory before starting the older version, or start the older version with `-salvagewallet`.

Package/version/hash/URL data above is copied from `depends/packages/*.mk` and `depends/hosts/darwin.mk` for convenience — it will drift if those files change without this table being updated too. Treat the `.mk` files as the source of truth.

## Vendored source files

Unlike the table above (external packages `depends` downloads and builds), these are single source files committed directly into this repo under their own upstream license, not this repo's MIT one -- each file's own header carries the authoritative license text; this table exists so that's recorded somewhere other than the file itself, per this repo's usual attribution practice.

| File | License | Source |
| --- | --- | --- |
| `src/test/fuzz/fuzz_code/FuzzedDataProvider.h` | Apache-2.0 WITH LLVM-exception | Part of the [LLVM Project](https://llvm.org/LICENSE.txt); see the file's own header for the SPDX identifier. |
| `src/tinyformat.h` | Boost Software License 1.0 | Chris Foster; see the file's own header for the full license text. |

Both are excluded from `test/lint/lint-filenames.sh`'s and `test/lint/lint-include-guards.sh`'s naming/include-guard checks, since neither is ours to rename or restructure.
