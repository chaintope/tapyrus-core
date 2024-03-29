packages:=boost libevent zeromq

qt_packages = qrencode

qt_linux_packages:=qt expat libxcb xcb_proto libXau xproto freetype fontconfig libxcb libxkbcommon

qt_darwin_packages=qt
qt_mingw32_packages=qt

wallet_packages=bdb

upnp_packages=miniupnpc

usdt_linux_packages=systemtap

darwin_native_packages = native_ds_store native_mac_alias

ifneq ($(build_os),darwin)
darwin_native_packages += native_cctools
endif

$(host_arch)_$(host_os)_native_packages += native_b2