ifneq ($(host),$(build))
host_toolchain:=$(host)-
endif

default_host_CC = $(host_toolchain)gcc
default_host_CXX = $(host_toolchain)g++
default_host_AR = $(host_toolchain)ar
default_host_RANLIB = $(host_toolchain)ranlib
default_host_STRIP = $(host_toolchain)strip
default_host_NM = $(host_toolchain)nm
default_host_OBJCOPY = $(host_toolchain)objcopy
default_host_OBJDUMP = $(host_toolchain)objdump

define add_host_tool_func
$(host_os)_$1?=$$(default_host_$1)
$(host_arch)_$(host_os)_$1?=$$($(host_os)_$1)
$(host_arch)_$(host_os)_$(release_type)_$1?=$$($(host_os)_$1)
host_$1=$$($(host_arch)_$(host_os)_$1)
endef

define add_host_flags_func
$(host_arch)_$(host_os)_$1 += $($(host_os)_$1)
$(host_arch)_$(host_os)_$(release_type)_$1 += $($(host_os)_$(release_type)_$1)
host_$1 = $$($(host_arch)_$(host_os)_$1)
host_$(release_type)_$1 = $$($(host_arch)_$(host_os)_$(release_type)_$1)
endef

$(foreach tool,CC CXX AR RANLIB STRIP NM OBJCOPY OBJDUMP DSYMUTIL,$(eval $(call add_host_tool_func,$(tool))))
$(foreach flags,CFLAGS CXXFLAGS CPPFLAGS LDFLAGS, $(eval $(call add_host_flags_func,$(flags))))
