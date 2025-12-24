package=qt
$(package)_version=6.10.1
$(package)_download_path=https://download.qt.io/official_releases/qt/6.10/$($(package)_version)/submodules
# Export version and download path for qttools and qttranslations
qt_version:=$($(package)_version)
qt_download_path:=$($(package)_download_path)
$(package)_suffix=everywhere-src-$($(package)_version).tar.xz
$(package)_file_name=qtbase-$($(package)_suffix)
$(package)_sha256_hash=5a6226f7e23db51fdc3223121eba53f3f5447cf0cc4d6cb82a3a2df7a65d265d
$(package)_linux_dependencies=freetype fontconfig libxcb libxkbcommon
$(package)_qt_libs=corelib network widgets gui plugins testlib
$(package)_patches = qtbase-moc-ignore-gcc-macro.patch
$(package)_patches += no_warnings_for_symbols.patch
$(package)_patches += rcc_hardcode_timestamp.patch
$(package)_patches += guix_cross_lib_path.patch
$(package)_patches += skip_xcode_version_check.patch

define $(package)_set_vars
# Qt 6 uses C++20 as minimum standard
$(package)_config_opts_release = -release
$(package)_config_opts_debug = -debug
$(package)_config_opts = -no-egl
$(package)_config_opts += -no-eglfs
$(package)_config_opts += -no-evdev
$(package)_config_opts += -no-gif
$(package)_config_opts += -no-glib
$(package)_config_opts += -no-icu
$(package)_config_opts += -no-ico
$(package)_config_opts += -no-kms
$(package)_config_opts += -no-linuxfb
$(package)_config_opts += -no-libjpeg
$(package)_config_opts += -no-libproxy
$(package)_config_opts += -no-libudev
$(package)_config_opts += -no-mtdev
$(package)_config_opts += -no-opengl
$(package)_config_opts += -no-openssl
$(package)_config_opts += -no-openvg
$(package)_config_opts += -no-reduce-relocations
$(package)_config_opts += -no-schannel
$(package)_config_opts += -no-sctp
$(package)_config_opts += -no-securetransport
$(package)_config_opts += -no-system-proxies
$(package)_config_opts += -no-use-gold-linker
$(package)_config_opts += -no-zstd
$(package)_config_opts += -nomake examples
$(package)_config_opts += -nomake tests
$(package)_config_opts += -prefix $(host_prefix)
$(package)_config_opts += -qt-doubleconversion
$(package)_config_opts += -qt-harfbuzz
ifneq ($(host),$(build))
$(package)_config_opts += -qt-host-path $(build_prefix)
endif
$(package)_config_opts += -qt-libpng
$(package)_config_opts += -qt-pcre
$(package)_config_opts += -qt-zlib
$(package)_config_opts += -static
$(package)_config_opts += -no-feature-backtrace
$(package)_config_opts += -no-feature-colordialog
$(package)_config_opts += -no-feature-concurrent
$(package)_config_opts += -no-feature-dial
$(package)_config_opts += -no-feature-gssapi
$(package)_config_opts += -no-feature-http
$(package)_config_opts += -no-feature-image_heuristic_mask
$(package)_config_opts += -no-feature-keysequenceedit
$(package)_config_opts += -no-feature-lcdnumber
$(package)_config_opts += -no-feature-libresolv
$(package)_config_opts += -no-feature-networkdiskcache
$(package)_config_opts += -no-feature-networkproxy
$(package)_config_opts += -no-feature-printsupport
$(package)_config_opts += -no-feature-sessionmanager
$(package)_config_opts += -no-feature-socks5
$(package)_config_opts += -no-feature-sql
$(package)_config_opts += -no-feature-textmarkdownreader
$(package)_config_opts += -no-feature-textmarkdownwriter
$(package)_config_opts += -no-feature-textodfwriter
$(package)_config_opts += -no-feature-topleveldomain
$(package)_config_opts += -no-feature-udpsocket
$(package)_config_opts += -no-feature-undocommand
$(package)_config_opts += -no-feature-undogroup
$(package)_config_opts += -no-feature-undostack
$(package)_config_opts += -no-feature-undoview
$(package)_config_opts += -no-feature-vnc
$(package)_config_opts += -no-feature-vulkan

# Core tools - these are in qtbase
$(package)_config_opts += -no-feature-androiddeployqt
$(package)_config_opts += -no-feature-macdeployqt
$(package)_config_opts += -no-feature-qmake
$(package)_config_opts += -no-feature-windeployqt

$(package)_config_opts_darwin = -no-dbus
$(package)_config_opts_darwin += -no-freetype
$(package)_config_opts_darwin += -no-fontconfig

$(package)_config_opts_linux = -xcb
$(package)_config_opts_linux += -no-xcb-xlib
$(package)_config_opts_linux += -no-feature-xlib
$(package)_config_opts_linux += -no-opengl
$(package)_config_opts_linux += -system-freetype
$(package)_config_opts_linux += -fontconfig
$(package)_config_opts_linux += -no-feature-vulkan
$(package)_config_opts_linux += -dbus-runtime

$(package)_config_opts_mingw32 = -no-opengl
$(package)_config_opts_mingw32 += -no-dbus
$(package)_config_opts_mingw32 += -no-freetype

# CMake build options
$(package)_cmake_opts = -DCMAKE_OSX_DEPLOYMENT_TARGET=$(OSX_MIN_VERSION)
ifeq ($(host_os),darwin)
$(package)_cmake_opts += -DQT_INTERNAL_APPLE_SDK_VERSION=$(OSX_SDK_VERSION)
$(package)_cmake_opts += -DQT_INTERNAL_XCODE_VERSION=$(XCODE_VERSION)
$(package)_cmake_opts += -DQT_NO_APPLE_SDK_MAX_VERSION_CHECK=ON
endif

ifneq ($(build_os),darwin)
$(package)_cmake_opts_darwin += -DCMAKE_SYSTEM_NAME=Darwin
$(package)_cmake_opts_darwin += -DCMAKE_OSX_SYSROOT=$(OSX_SDK)
endif

ifneq ($(build_arch),$(host_arch))
$(package)_cmake_opts_arm64_darwin += -DCMAKE_OSX_ARCHITECTURES=arm64
$(package)_cmake_opts_x86_64_darwin += -DCMAKE_OSX_ARCHITECTURES=x86_64
endif

ifneq ($(LTO),)
$(package)_cmake_opts_linux += -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
$(package)_cmake_opts_mingw32 += -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
endif

$(package)_cmake_opts_mingw32 += -DCMAKE_SYSTEM_NAME=Windows
$(package)_cmake_opts_mingw32 += -DCMAKE_C_COMPILER=$($(package)_cc)
$(package)_cmake_opts_mingw32 += -DCMAKE_CXX_COMPILER=$($(package)_cxx)
endef

define $(package)_fetch_cmds
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_download_file),$($(package)_file_name),$($(package)_sha256_hash))
endef

define $(package)_extract_cmds
  mkdir -p $($(package)_extract_dir) && \
  echo "$($(package)_sha256_hash)  $($(package)_source)" > $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  $(build_SHA256SUM) -c $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  mkdir qtbase && \
  $(build_TAR) -x -f $($(package)_source) -C qtbase --strip-components=1 --no-same-owner
endef

# Qt 6 uses CMake. Apply only the patches that are still relevant.
define $(package)_preprocess_cmds
  patch -p1 -i $($(package)_patch_dir)/qtbase-moc-ignore-gcc-macro.patch && \
  patch -p1 -i $($(package)_patch_dir)/no_warnings_for_symbols.patch && \
  patch -p1 -i $($(package)_patch_dir)/rcc_hardcode_timestamp.patch && \
  patch -p1 -i $($(package)_patch_dir)/guix_cross_lib_path.patch && \
  patch -p1 -i $($(package)_patch_dir)/skip_xcode_version_check.patch
endef

define $(package)_config_cmds
  export QT_MAC_SDK_NO_VERSION_CHECK=1 && \
  export PKG_CONFIG_LIBDIR=$(host_prefix)/lib/pkgconfig && \
  export PKG_CONFIG_PATH=$(host_prefix)/share/pkgconfig && \
  cd qtbase && \
  ./configure $($(package)_config_opts) -- $($(package)_cmake_opts)
endef

define $(package)_build_cmds
  cd qtbase && cmake --build .
endef

define $(package)_stage_cmds
  cd qtbase && cmake --install . --prefix $($(package)_staging_prefix_dir)
endef

define $(package)_postprocess_cmds
  rm -rf doc/ native/lib/ && \
  rm -f lib/lib*.la && \
  rm -f lib/cmake/Qt6/qt.toolchain.cmake
endef
