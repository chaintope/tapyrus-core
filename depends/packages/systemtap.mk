package=systemtap
$(package)_version=5.5
$(package)_download_path=https://sourceware.org/systemtap/ftp/releases/
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=980e58887a284097b9d4c6ae6382b75787573131c27e3875c0fc94bceb8c61a8
$(package)_patches=remove_SDT_ASM_SECTION_AUTOGROUP_SUPPORT_check.patch

define $(package)_preprocess_cmds
  patch -p1 < $($(package)_patch_dir)/remove_SDT_ASM_SECTION_AUTOGROUP_SUPPORT_check.patch && \
  mkdir -p $($(package)_staging_prefix_dir)/include/sys && \
  cp includes/sys/sdt.h $($(package)_staging_prefix_dir)/include/sys/sdt.h
endef
