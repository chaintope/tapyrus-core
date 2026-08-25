Dependencies
============

These are the dependencies currently used by Tapyrus Core. You can find instructions for installing them in the `build-*.md` file for your platform.

|Dependency | Minimum required |
|----|----|
|CMake|3.28|
|Clang|18.1.3|
|GCC|13.2.0|
|Python (scripts, tests)|3.10|

| Package | Version | File name | SHA256 | Download URL |
| --- | --- | --- | --- | --- |
| Berkeley DB | 4.8.30 | `db-4.8.30.NC.tar.gz` | `12edc0df75bf9abd7f82f821795bcee50f42cb2e5f76a6a281b85732798364ef` | [download.oracle.com](https://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz) |
| Boost | 1.81.0 | `boost_1_81_0.tar.gz` | `205666dea9f6a7cfed87c7a6dfbeb52a2c1b9de55712c9c1a87735d7181452b6` | [archives.boost.io](https://archives.boost.io/release/1.81.0/source/boost_1_81_0.tar.gz) |
| Expat | 2.4.8 | `expat-2.4.8.tar.xz` | `f79b8f904b749e3e0d20afeadecf8249c55b2e32d4ebb089ae378df479dcaf25` | [github.com](https://github.com/libexpat/libexpat/releases/download/R_2_4_8/expat-2.4.8.tar.xz) |
| fontconfig | 2.12.6 | `fontconfig-2.12.6.tar.bz2` | `cf0c30807d08f6a28ab46c61b8dbd55c97d2f292cf88f3a07d3384687f31f017` | [freedesktop.org](https://www.freedesktop.org/software/fontconfig/release/fontconfig-2.12.6.tar.bz2) |
| FreeType | 2.11.0 | `freetype-2.11.0.tar.xz` | `8bee39bd3968c4804b70614a0a3ad597299ad0e824bc8aad5ce8aaf48067bde7` | [savannah.gnu.org](https://download.savannah.gnu.org/releases/freetype/freetype-2.11.0.tar.xz) |
| libevent | 2.1.12-stable | `libevent-2.1.12-stable.tar.gz` | `92e6de1be9ec176428fd2367677e61ceffc2ee1cb119035037a27d346b0403bb` | [github.com](https://github.com/libevent/libevent/releases/download/release-2.1.12-stable/libevent-2.1.12-stable.tar.gz) |
| libXau | 1.0.12 | `libXau-1.0.12.tar.gz` | `2402dd938da4d0a332349ab3d3586606175e19cb32cb9fe013c19f1dc922dcee` | [xorg.freedesktop.org](https://xorg.freedesktop.org/releases/individual/lib/libXau-1.0.12.tar.gz) |
| MiniUPnPc | 2.3.3 | `miniupnpc-2.3.3.tar.gz` | `d52a0afa614ad6c088cc9ddff1ae7d29c8c595ac5fdd321170a05f41e634bd1a` | [github.com](https://github.com/miniupnp/miniupnp/releases/download/miniupnpc_2_3_3/miniupnpc-2.3.3.tar.gz) |
| qrencode | 4.1.1 | `qrencode-4.1.1.tar.bz2` | `e455d9732f8041cf5b9c388e345a641fd15707860f928e94507b1961256a6923` | [fukuchi.org](https://fukuchi.org/works/qrencode/qrencode-4.1.1.tar.bz2) |
| Qt (qtbase) | 6.10.1 | `qtbase-everywhere-src-6.10.1.tar.xz` | `5a6226f7e23db51fdc3223121eba53f3f5447cf0cc4d6cb82a3a2df7a65d265d` | [download.qt.io](https://download.qt.io/official_releases/qt/6.10/6.10.1/submodules/qtbase-everywhere-src-6.10.1.tar.xz) |
| Qt (qttranslations) | 6.10.1 | `qttranslations-everywhere-src-6.10.1.tar.xz` | `8e49a2df88a12c376a479ae7bd272a91cf57ebb4e7c0cf7341b3565df99d2314` | [download.qt.io](https://download.qt.io/official_releases/qt/6.10/6.10.1/submodules/qttranslations-everywhere-src-6.10.1.tar.xz) |
| Qt (qttools) | 6.10.1 | `qttools-everywhere-src-6.10.1.tar.xz` | `8148408380ffea03101a26305c812b612ea30dbc07121e58707601522404d49b` | [download.qt.io](https://download.qt.io/official_releases/qt/6.10/6.10.1/submodules/qttools-everywhere-src-6.10.1.tar.xz) |
| systemtap (Linux only) | 4.7 | `systemtap-4.7.tar.gz` | `43a0a3db91aa4d41e28015b39a65e62059551f3cc7377ebf3a3a5ca7339e7b1f` | [sourceware.org](https://sourceware.org/systemtap/ftp/releases/systemtap-4.7.tar.gz) |
| libxcb (Linux only) | 1.17.0 | `libxcb-1.17.0.tar.gz` | `2c69287424c9e2128cb47ffe92171e10417041ec2963bceafb65cb3fcf8f0b85` | [xcb.freedesktop.org](https://xcb.freedesktop.org/dist/libxcb-1.17.0.tar.gz) |
| libxcb_util_cursor (Linux only) | 0.1.6 | `xcb-util-cursor-0.1.6.tar.gz` | `eae38b2dfc5c529a886e507ef576b12d2a20aa1f149608e4853af760f31be60b` | [xcb.freedesktop.org](https://xcb.freedesktop.org/dist/xcb-util-cursor-0.1.6.tar.gz) |
| libxcb_util_image (Linux only) | 0.4.1 | `xcb-util-image-0.4.1.tar.gz` | `0ebd4cf809043fdeb4f980d58cdcf2b527035018924f8c14da76d1c81001293b` | [xcb.freedesktop.org](https://xcb.freedesktop.org/dist/xcb-util-image-0.4.1.tar.gz) |
| libxcb_util_keysyms (Linux only) | 0.4.1 | `xcb-util-keysyms-0.4.1.tar.gz` | `1fa21c0cea3060caee7612b6577c1730da470b88cbdf846fa4e3e0ff78948e54` | [xcb.freedesktop.org](https://xcb.freedesktop.org/dist/xcb-util-keysyms-0.4.1.tar.gz) |
| libxcb_util_render (Linux only) | 0.3.10 | `xcb-util-renderutil-0.3.10.tar.gz` | `e04143c48e1644c5e074243fa293d88f99005b3c50d1d54358954404e635128a` | [xcb.freedesktop.org](https://xcb.freedesktop.org/dist/xcb-util-renderutil-0.3.10.tar.gz) |
| libxcb_util_wm (Linux only) | 0.4.2 | `xcb-util-wm-0.4.2.tar.gz` | `dcecaaa535802fd57c84cceeff50c64efe7f2326bf752e16d2b77945649c8cd7` | [xcb.freedesktop.org](https://xcb.freedesktop.org/dist/xcb-util-wm-0.4.2.tar.gz) |
| libxcb_util (Linux only) | 0.4.1 | `xcb-util-0.4.1.tar.gz` | `21c6e720162858f15fe686cef833cf96a3e2a79875f84007d76f6d00417f593a` | [xcb.freedesktop.org](https://xcb.freedesktop.org/dist/xcb-util-0.4.1.tar.gz) |
| xkbcommon (Linux only) | 1.7.0 | `libxkbcommon-1.7.0.tar.xz` | `65782f0a10a4b455af9c6baab7040e2f537520caa2ec2092805cdfd36863b247` | [xkbcommon.org](https://xkbcommon.org/download/libxkbcommon-1.7.0.tar.xz) |
| xcb_proto | 1.17.0 | `xcb-proto-1.17.0.tar.gz` | `392d3c9690f8c8202a68fdb89c16fd55159ab8d65000a6da213f4a1576e97a16` | [xorg.freedesktop.org](https://xorg.freedesktop.org/archive/individual/proto/xcb-proto-1.17.0.tar.gz) |
| xproto | 7.0.31 | `xproto-7.0.31.tar.gz` | `6d755eaae27b45c5cc75529a12855fed5de5969b367ed05003944cf901ed43c7` | [xorg.freedesktop.org](https://xorg.freedesktop.org/releases/individual/proto/xproto-7.0.31.tar.gz) |
| ZeroMQ | 4.3.5 | `zeromq-4.3.5.tar.gz` | `6653ef5910f17954861fe72332e68b03ca6e4d9c7160eb3a8de5a5a913bfab43` | [github.com](https://github.com/zeromq/libzmq/releases/download/v4.3.5/zeromq-4.3.5.tar.gz) |

## S3 fallback mirror

`depends` downloads each package above from its "Download URL". If that primary host is unreachable, it automatically falls back to `FALLBACK_DOWNLOAD_PATH` (see `depends/Makefile`), an S3 mirror at [https://s3-ap-northeast-1.amazonaws.com/repo.tapyrus.chaintope.com](https://s3-ap-northeast-1.amazonaws.com/repo.tapyrus.chaintope.com). The mirror is flat (no subdirectories) and must serve each file under the exact "File name" listed in the table above.

Building Qt also requires three small CMake scaffolding files that `depends` fetches directly from the `qt/qt5` GitHub repo rather than from a Qt release archive (see `depends/packages/qt_details.mk`). These are intentionally not mirrored on S3, since they're read from a branch ref on GitHub rather than a pinned release artifact and could change upstream at any time.
