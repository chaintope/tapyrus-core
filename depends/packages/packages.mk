packages:=boost libevent zeromq

qt_packages = qrencode

qt_linux_packages:=qt expat libxcb xcb_proto libXau xproto freetype fontconfig

qt_darwin_packages=qt
qt_mingw32_packages=qt

wallet_packages=bdb

upnp_packages=miniupnpc

darwin_native_packages = native_ds_store native_mac_alias

ifneq ($(build_os),darwin)
darwin_native_packages += native_cctools native_libdmg-hfsplus
endif
