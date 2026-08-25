package=fontconfig
$(package)_version=2.16.0
$(package)_download_path=https://www.freedesktop.org/software/fontconfig/release/
# upstream stopped publishing .tar.bz2 releases after 2.13.x, .tar.xz only now
$(package)_file_name=$(package)-$($(package)_version).tar.xz
$(package)_sha256_hash=6a33dc555cc9ba8b10caf7695878ef134eeb36d0af366041f639b1da9b6ed220
$(package)_dependencies=freetype expat
$(package)_patches=gperf_header_regen.patch

define $(package)_set_vars
  $(package)_config_opts=--disable-docs --disable-static --disable-libxml2 --disable-iconv
  $(package)_config_opts += --disable-dependency-tracking --enable-option-checking
  $(package)_cflags += -Wno-implicit-function-declaration
endef

define $(package)_preprocess_cmds
  patch -p1 < $($(package)_patch_dir)/gperf_header_regen.patch
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
  rm -rf var lib/*.la
endef
