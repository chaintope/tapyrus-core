package=bdb
$(package)_version=5.3.28
$(package)_download_path=https://download.oracle.com/berkeley-db
$(package)_file_name=db-$($(package)_version).NC.tar.gz
$(package)_sha256_hash=76a25560d9e52a198d37a31440fd07632b5f1f8f9f2b6d5438f4bc3e7c9013ef
$(package)_build_subdir=build_unix
ifeq ($($(package)_version),4.8.30)
$(package)_patches=clang_cxx_11.patch
endif
ifeq ($($(package)_version),5.3.28)
$(package)_patches=clang_cxx_11.patch mingw_winioctl_case.patch
endif

define $(package)_set_vars
$(package)_config_opts=--disable-shared --enable-cxx --disable-replication --enable-option-checking
$(package)_config_opts_mingw32=--enable-mingw
$(package)_cflags+=-Wno-error=implicit-function-declaration -Wno-error=format-security -Wno-error=implicit-int
$(package)_cppflags_mingw32=-DUNICODE -D_UNICODE
endef

define $(package)_preprocess_cmds
  $(foreach patch,$($(package)_patches),patch -p1 < $($(package)_patch_dir)/$(patch) &&) \
  cp -f $(BASEDIR)/config.guess $(BASEDIR)/config.sub dist
endef

define $(package)_config_cmds
  ../dist/$($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE) libdb_cxx-5.3.a libdb-5.3.a
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install_lib install_include
endef
