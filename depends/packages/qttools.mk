package=qttools
$(package)_version=$(qt_version)
$(package)_download_path=$(qt_download_path)
$(package)_suffix=everywhere-src-$($(package)_version).tar.xz
$(package)_file_name=qttools-$($(package)_suffix)
$(package)_sha256_hash=8148408380ffea03101a26305c812b612ea30dbc07121e58707601522404d49b
$(package)_dependencies=qt
$(package)_linguist_tools = lrelease lupdate lconvert

define $(package)_set_vars
# CMake build options
$(package)_cmake_opts = -DCMAKE_OSX_DEPLOYMENT_TARGET=$(OSX_MIN_VERSION)
$(package)_cmake_opts += -DCMAKE_PREFIX_PATH=$(host_prefix)

# Qt6 modules need QT_HOST_PATH to find host tools and Qt6HostInfo
ifneq ($(host),$(build))
$(package)_cmake_opts += -DQT_HOST_PATH=$(build_prefix)
else
$(package)_cmake_opts += -DQT_HOST_PATH=$(host_prefix)
endif

# Disable features we don't need
$(package)_cmake_opts += -DFEATURE_assistant=OFF
$(package)_cmake_opts += -DFEATURE_designer=OFF
$(package)_cmake_opts += -DFEATURE_pixeltool=OFF
$(package)_cmake_opts += -DFEATURE_distancefieldgenerator=OFF
$(package)_cmake_opts += -DFEATURE_qtattributionsscanner=OFF
$(package)_cmake_opts += -DFEATURE_qtdiag=OFF
$(package)_cmake_opts += -DFEATURE_qtplugininfo=OFF
# Only build linguist tools (lrelease, lupdate, lconvert)
$(package)_cmake_opts += -DFEATURE_linguist=ON
# Disable QML support in lupdate to avoid dependency on QtQml
$(package)_cmake_opts += -DFEATURE_clangcpp=OFF

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
  mkdir qttools && \
  $(build_TAR) -x -f $($(package)_source) -C qttools --strip-components=1 --no-same-owner
endef

define $(package)_preprocess_cmds
  echo "No patches needed for qttools"
endef

define $(package)_config_cmds
  export QT_MAC_SDK_NO_VERSION_CHECK=1 && \
  export PKG_CONFIG_LIBDIR=$(host_prefix)/lib/pkgconfig && \
  export PKG_CONFIG_PATH=$(host_prefix)/share/pkgconfig && \
  cd qttools && \
  cmake -S . -B build $($(package)_cmake_opts)
endef

define $(package)_build_cmds
  cd qttools/build && cmake --build . --target lrelease lupdate lconvert
endef

define $(package)_stage_cmds
  cd qttools/build && cmake --install . --prefix $($(package)_staging_prefix_dir)
endef

define $(package)_postprocess_cmds
  rm -rf doc/ && \
  rm -f lib/lib*.la
endef
