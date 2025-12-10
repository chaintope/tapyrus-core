package=qt
$(package)_version=6.10.1
$(package)_download_path=https://download.qt.io/official_releases/qt/6.10/$($(package)_version)/submodules
$(package)_suffix=everywhere-src-$($(package)_version).tar.xz
$(package)_file_name=qtbase-$($(package)_suffix)
$(package)_sha256_hash=5a6226f7e23db51fdc3223121eba53f3f5447cf0cc4d6cb82a3a2df7a65d265d
$(package)_linux_dependencies=freetype fontconfig libxcb libxkbcommon
$(package)_qt_libs=corelib network widgets gui plugins testlib
$(package)_linguist_tools = lrelease lupdate lconvert
$(package)_patches = qtbase-moc-ignore-gcc-macro.patch
$(package)_patches += no_warnings_for_symbols.patch
$(package)_patches += rcc_hardcode_timestamp.patch
$(package)_patches += guix_cross_lib_path.patch
$(package)_patches += memory_resource.patch
$(package)_patches += clang_18_libpng.patch
$(package)_patches += windows_lto.patch
$(package)_patches += fix_activity_logging.patch
$(package)_patches += fix_os_log_deprecated.patch

$(package)_qttranslations_file_name=qttranslations-$($(package)_suffix)
$(package)_qttranslations_sha256_hash=8e49a2df88a12c376a479ae7bd272a91cf57ebb4e7c0cf7341b3565df99d2314

$(package)_qttools_file_name=qttools-$($(package)_suffix)
$(package)_qttools_sha256_hash=8148408380ffea03101a26305c812b612ea30dbc07121e58707601522404d49b

$(package)_extra_sources  = $($(package)_qttranslations_file_name)
$(package)_extra_sources += $($(package)_qttools_file_name)

define $(package)_set_vars
$(package)_config_env = QT_MAC_SDK_NO_VERSION_CHECK=1
$(package)_cmake_opts_release = -release
$(package)_cmake_opts_release += -silent
$(package)_cmake_opts_debug = -debug
$(package)_cmake_opts_debug += -optimized-tools
$(package)_cmake_opts += -bindir $(build_prefix)/bin
# Qt 6 uses C++20 as minimum standard
# Filter out -std= from cxxflags since Qt's CMake will control the standard
$(package)_cxxflags:=$(filter-out -std=%,$(host_CXXFLAGS))
$(package)_cmake_opts += -c++std c++20
$(package)_cmake_opts += -confirm-license
$(package)_cmake_opts += -- -DQT_HOST_PATH=$(build_prefix)
$(package)_cmake_opts += -no-compile-examples
$(package)_cmake_opts += -no-cups
$(package)_cmake_opts += -no-egl
$(package)_cmake_opts += -no-eglfs
$(package)_cmake_opts += -no-evdev
$(package)_cmake_opts += -no-gif
$(package)_cmake_opts += -no-glib
$(package)_cmake_opts += -no-icu
$(package)_cmake_opts += -no-ico
$(package)_cmake_opts += -no-iconv
$(package)_cmake_opts += -no-kms
$(package)_cmake_opts += -no-linuxfb
$(package)_cmake_opts += -no-libjpeg
$(package)_cmake_opts += -no-libproxy
$(package)_cmake_opts += -no-libudev
$(package)_cmake_opts += -no-mimetype-database
$(package)_cmake_opts += -no-mtdev
$(package)_cmake_opts += -no-openssl
$(package)_cmake_opts += -no-openvg
$(package)_cmake_opts += -no-schannel
$(package)_cmake_opts += -no-sctp
$(package)_cmake_opts += -no-securetransport
$(package)_cmake_opts += -no-sql-db2
$(package)_cmake_opts += -no-sql-ibase
$(package)_cmake_opts += -no-sql-oci
$(package)_cmake_opts += -no-sql-tds
$(package)_cmake_opts += -no-sql-mysql
$(package)_cmake_opts += -no-sql-odbc
$(package)_cmake_opts += -no-sql-psql
$(package)_cmake_opts += -no-sql-sqlite
$(package)_cmake_opts += -no-sql-sqlite2
$(package)_cmake_opts += -no-system-proxies
$(package)_cmake_opts += -no-use-gold-linker
$(package)_cmake_opts += -no-zstd
$(package)_cmake_opts += -nomake examples
$(package)_cmake_opts += -nomake tests
$(package)_cmake_opts += -nomake tools
$(package)_cmake_opts += -opensource
$(package)_cmake_opts += -prefix $(host_prefix)
$(package)_cmake_opts += -qt-libpng
$(package)_cmake_opts += -qt-pcre
$(package)_cmake_opts += -qt-harfbuzz
$(package)_cmake_opts += -qt-zlib
$(package)_cmake_opts += -static
$(package)_cmake_opts += -optimize-size
$(package)_cmake_opts += -v
$(package)_cmake_opts += -no-feature-bearermanagement
$(package)_cmake_opts += -no-feature-colordialog
$(package)_cmake_opts += -no-feature-commandlineparser
$(package)_cmake_opts += -no-feature-concurrent
$(package)_cmake_opts += -no-feature-dial
$(package)_cmake_opts += -no-feature-fontcombobox
$(package)_cmake_opts += -no-feature-ftp
$(package)_cmake_opts += -no-feature-http
$(package)_cmake_opts += -no-feature-image_heuristic_mask
$(package)_cmake_opts += -no-feature-keysequenceedit
$(package)_cmake_opts += -no-feature-lcdnumber
$(package)_cmake_opts += -no-feature-networkdiskcache
$(package)_cmake_opts += -no-feature-networkproxy
$(package)_cmake_opts += -no-feature-pdf
$(package)_cmake_opts += -no-feature-printdialog
$(package)_cmake_opts += -no-feature-printer
$(package)_cmake_opts += -no-feature-printpreviewdialog
$(package)_cmake_opts += -no-feature-printpreviewwidget
$(package)_cmake_opts += -no-feature-sessionmanager
$(package)_cmake_opts += -no-feature-socks5
$(package)_cmake_opts += -no-feature-sql
$(package)_cmake_opts += -no-feature-sqlmodel
$(package)_cmake_opts += -no-feature-statemachine
$(package)_cmake_opts += -no-feature-syntaxhighlighter
$(package)_cmake_opts += -no-feature-textbrowser
$(package)_cmake_opts += -no-feature-textmarkdownwriter
$(package)_cmake_opts += -no-feature-textodfwriter
$(package)_cmake_opts += -no-feature-topleveldomain
$(package)_cmake_opts += -no-feature-udpsocket
$(package)_cmake_opts += -no-feature-undocommand
$(package)_cmake_opts += -no-feature-undogroup
$(package)_cmake_opts += -no-feature-undostack
$(package)_cmake_opts += -no-feature-undoview
$(package)_cmake_opts += -no-feature-vnc
$(package)_cmake_opts += -no-feature-wizard
$(package)_cmake_opts += -no-feature-xml

$(package)_cmake_opts_darwin = -no-dbus
$(package)_cmake_opts_darwin += -no-opengl
$(package)_cmake_opts_darwin += -no-feature-corewlan
$(package)_cmake_opts_darwin += -no-freetype
$(package)_cmake_opts_darwin += -no-fontconfig
$(package)_cmake_opts_darwin += -- -DCMAKE_OSX_DEPLOYMENT_TARGET=$(OSX_MIN_VERSION)

ifneq ($(build_os),darwin)
$(package)_cmake_opts_darwin += -- -DCMAKE_SYSTEM_NAME=Darwin
$(package)_cmake_opts_darwin += -DCMAKE_OSX_SYSROOT=$(OSX_SDK)
endif

ifneq ($(build_arch),$(host_arch))
$(package)_cmake_opts_arm64_darwin += -- -DCMAKE_OSX_ARCHITECTURES=arm64
$(package)_cmake_opts_x86_64_darwin += -- -DCMAKE_OSX_ARCHITECTURES=x86_64
endif

$(package)_cmake_opts_linux = -xcb
$(package)_cmake_opts_linux += -no-xcb-xlib
$(package)_cmake_opts_linux += -no-feature-xlib
$(package)_cmake_opts_linux += -no-opengl
$(package)_cmake_opts_linux += -system-freetype
$(package)_cmake_opts_linux += -fontconfig
$(package)_cmake_opts_linux += -no-feature-vulkan
$(package)_cmake_opts_linux += -dbus-runtime
ifneq ($(LTO),)
$(package)_cmake_opts_linux += -- -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
endif

$(package)_cmake_opts_mingw32 = -no-opengl
$(package)_cmake_opts_mingw32 += -no-dbus
$(package)_cmake_opts_mingw32 += -no-freetype
$(package)_cmake_opts_mingw32 += -- -DCMAKE_SYSTEM_NAME=Windows
$(package)_cmake_opts_mingw32 += -DCMAKE_C_COMPILER=$($(package)_cc)
$(package)_cmake_opts_mingw32 += -DCMAKE_CXX_COMPILER=$($(package)_cxx)
ifneq ($(LTO),)
$(package)_cmake_opts_mingw32 += -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
endif
endef

define $(package)_fetch_cmds
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_download_file),$($(package)_file_name),$($(package)_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_qttranslations_file_name),$($(package)_qttranslations_file_name),$($(package)_qttranslations_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_qttools_file_name),$($(package)_qttools_file_name),$($(package)_qttools_sha256_hash))
endef

define $(package)_extract_cmds
  mkdir -p $($(package)_extract_dir) && \
  echo "$($(package)_sha256_hash)  $($(package)_source)" > $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_qttranslations_sha256_hash)  $($(package)_source_dir)/$($(package)_qttranslations_file_name)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_qttools_sha256_hash)  $($(package)_source_dir)/$($(package)_qttools_file_name)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  $(build_SHA256SUM) -c $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  mkdir qtbase && \
  $(build_TAR) -x -f $($(package)_source) -C qtbase --strip-components=1 --no-same-owner && \
  mkdir qttranslations && \
  $(build_TAR) -x -f $($(package)_source_dir)/$($(package)_qttranslations_file_name) -C qttranslations --strip-components=1 --no-same-owner && \
  mkdir qttools && \
  $(build_TAR) -x -f $($(package)_source_dir)/$($(package)_qttools_file_name) -C qttools --strip-components=1 --no-same-owner
endef

# Qt 6 uses CMake. Apply only the patches that are still relevant.
define $(package)_preprocess_cmds
  patch -p1 -i $($(package)_patch_dir)/qtbase-moc-ignore-gcc-macro.patch && \
  patch -p1 -i $($(package)_patch_dir)/memory_resource.patch && \
  patch -p1 -i $($(package)_patch_dir)/no_warnings_for_symbols.patch && \
  patch -p1 -i $($(package)_patch_dir)/clang_18_libpng.patch && \
  patch -p1 -i $($(package)_patch_dir)/rcc_hardcode_timestamp.patch && \
  patch -p1 -i $($(package)_patch_dir)/guix_cross_lib_path.patch && \
  patch -p1 -i $($(package)_patch_dir)/windows_lto.patch && \
  patch -p1 -i $($(package)_patch_dir)/fix_activity_logging.patch && \
  patch -p1 -i $($(package)_patch_dir)/fix_os_log_deprecated.patch
endef

define $(package)_config_cmds
  export QT_MAC_SDK_NO_VERSION_CHECK=1 && \
  export PKG_CONFIG_LIBDIR=$(host_prefix)/lib/pkgconfig && \
  export PKG_CONFIG_PATH=$(host_prefix)/share/pkgconfig && \
  cd qtbase && \
  ./configure -top-level $($(package)_cmake_opts)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) INSTALL_ROOT=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
  rm -rf doc/ native/lib/ && \
  rm -f lib/lib*.la && \
  rm -f lib/cmake/Qt6/qt.toolchain.cmake
endef
