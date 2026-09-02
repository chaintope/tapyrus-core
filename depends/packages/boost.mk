package=boost
$(package)_version=1.92.0
$(package)_download_path=https://archives.boost.io/release/$($(package)_version)/source/
$(package)_file_name=boost_$(subst .,_,$($(package)_version)).tar.gz
$(package)_sha256_hash=c4a3b310ddd2472416e091067166b0713be97c63f38c212c484ada022fd296ce

define $(package)_stage_cmds
  mkdir -p $($(package)_staging_prefix_dir)/include && \
  cp -r boost $($(package)_staging_prefix_dir)/include
endef
