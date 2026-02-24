package=libxkbcommon
$(package)_version=1.7.0
$(package)_download_path=https://xkbcommon.org/download/
$(package)_file_name=$(package)-$($(package)_version).tar.xz
$(package)_sha256_hash=65782f0a10a4b455af9c6baab7040e2f537520caa2ec2092805cdfd36863b247
$(package)_dependencies=libxcb

# Version >= 0.9.0 uses Meson (required by Qt 6.10+ which needs xkb_x11_state_new_from_device).
define $(package)_set_vars
$(package)_config_env := CC="$$($(package)_cc)"
endef

define $(package)_config_cmds
  python3 -c "cc='$($(package)_cc)'.split(); open('meson_cross.ini','w').write('[binaries]\nc = ' + repr(cc) + '\npkg-config = \'pkg-config\'\n\n[properties]\nc_args = [\'-I$(host_prefix)/include\']\nc_link_args = [\'-L$(host_prefix)/lib\']\n\n[host_machine]\nsystem = \'linux\'\ncpu_family = \'$(host_arch)\'\ncpu = \'$(host_arch)\'\nendian = \'little\'\n')" && \
  meson setup \
    --cross-file meson_cross.ini \
    --prefix=$(host_prefix) \
    --buildtype=release \
    --default-library=static \
    -Denable-docs=false \
    -Denable-tools=false \
    -Denable-wayland=false \
    -Denable-xkbregistry=false \
    -Denable-bash-completion=false \
    -Denable-x11=true \
    -Dxkb-config-root=/usr/share/X11/xkb \
    builddir .
endef

define $(package)_build_cmds
  ninja -C builddir $(filter -j%,$(MAKEFLAGS)) libxkbcommon.a libxkbcommon-x11.a
endef

define $(package)_stage_cmds
  DESTDIR=$($(package)_staging_dir) meson install -C builddir --no-rebuild
endef

define $(package)_postprocess_cmds
  rm -rf share/
endef
