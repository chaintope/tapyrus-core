package=zeromq
$(package)_version=4.3.5
$(package)_download_path=https://github.com/zeromq/libzmq/releases/download/v$($(package)_version)/
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=6653ef5910f17954861fe72332e68b03ca6e4d9c7160eb3a8de5a5a913bfab43
$(package)_build_subdir=build
ifeq ($($(package)_version),4.3.5)
# Order matters: most of these patch the same CMakeLists.txt in sequence, so
# this must match the exact order patches are applied in (preprocess_cmds
# below), not just be any permutation containing all seven.
$(package)_patches  = macos_mktemp_check.patch
$(package)_patches += builtin_sha1.patch
$(package)_patches += cacheline_undefined.patch
$(package)_patches += fix_have_windows.patch
$(package)_patches += openbsd_kqueue_headers.patch
$(package)_patches += cmake_minimum.patch
$(package)_patches += no_librt.patch
endif

define $(package)_set_vars
  $(package)_cmake_opts := -DCMAKE_BUILD_TYPE=None -DWITH_DOCS=OFF -DWITH_LIBSODIUM=OFF
  $(package)_cmake_opts += -DWITH_LIBBSD=OFF -DENABLE_CURVE=OFF -DENABLE_CPACK=OFF
  $(package)_cmake_opts += -DBUILD_SHARED=OFF -DBUILD_TESTS=OFF -DZMQ_BUILD_TESTS=OFF
  $(package)_cmake_opts += -DENABLE_DRAFTS=OFF -DZMQ_BUILD_TESTS=OFF
  $(package)_cxxflags += -fdebug-prefix-map=$($(package)_extract_dir)=/usr -fmacro-prefix-map=$($(package)_extract_dir)=/usr
  $(package)_cmake_opts_mingw32 += -DZMQ_WIN32_WINNT=0x0A00 -DZMQ_HAVE_IPC=OFF
endef

define $(package)_preprocess_cmds
  $(foreach patch,$($(package)_patches),patch -p1 < $($(package)_patch_dir)/$(patch) &&) true
endef

define $(package)_config_cmds
   $($(package)_cmake) -S .. -B .
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
  rm -rf share && \
  rm -rf lib/pkgconfig && \
  rm -f lib/*.dylib*
endef
