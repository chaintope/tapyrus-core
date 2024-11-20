package=boost
$(package)_version=1.83.0
$(package)_download_path=https://archives.boost.io/release/$($(package)_version)/source/
$(package)_file_name=boost_$(subst .,_,$($(package)_version)).tar.gz
$(package)_sha256_hash=c0685b68dd44cc46574cce86c4e17c0f611b15e195be9848dfd0769a0a207628

define $(package)_extract_cmds
  mkdir -p $($(package)_extract_dir) && \
  tar --no-same-owner --strip-components=1 -xf $($(package)_source)
endef

define $(package)_stage_cmds
  mkdir -p $($(package)_staging_prefix_dir)/include && \
  cp -r boost $($(package)_staging_prefix_dir)/include
endef
