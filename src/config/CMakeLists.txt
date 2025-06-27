# Copyright (c) 2017-2019 The Bitcoin developers

# This generates config.h which provides numerous defines
# about the state of the plateform we are building on.

include(CheckIncludeFiles)
include(CheckSymbolExists)
include(CheckCXXSourceCompiles)

# Endianness
check_include_files("endian.h" HAVE_ENDIAN_H)
check_include_files("sys/endian.h" HAVE_SYS_ENDIAN_H)

if(HAVE_ENDIAN_H)
	set(ENDIAN_FILE "endian.h")
elseif(HAVE_SYS_ENDIAN_H)
	set(ENDIAN_FILE "sys/endian.h")
else()
endif()

if(ENDIAN_FILE)
	check_symbol_exists(htole16 ${ENDIAN_FILE} HAVE_DECL_HTOLE16)
	check_symbol_exists(htobe16 ${ENDIAN_FILE} HAVE_DECL_HTOBE16)
	check_symbol_exists(be16toh ${ENDIAN_FILE} HAVE_DECL_BE16TOH)
	check_symbol_exists(le16toh ${ENDIAN_FILE} HAVE_DECL_LE16TOH)
	check_symbol_exists(htobe32 ${ENDIAN_FILE} HAVE_DECL_HTOBE32)
	check_symbol_exists(htole32 ${ENDIAN_FILE} HAVE_DECL_HTOLE32)
	check_symbol_exists(be32toh ${ENDIAN_FILE} HAVE_DECL_BE32TOH)
	check_symbol_exists(le32toh ${ENDIAN_FILE} HAVE_DECL_LE32TOH)
	check_symbol_exists(htobe64 ${ENDIAN_FILE} HAVE_DECL_HTOBE64)
	check_symbol_exists(htole64 ${ENDIAN_FILE} HAVE_DECL_HTOLE64)
	check_symbol_exists(be64toh ${ENDIAN_FILE} HAVE_DECL_BE64TOH)
	check_symbol_exists(le64toh ${ENDIAN_FILE} HAVE_DECL_LE64TOH)
endif()

# Byte swap
check_include_files("byteswap.h" HAVE_BYTESWAP_H)

check_symbol_exists(bswap_16 "byteswap.h" HAVE_DECL_BSWAP_16)
check_symbol_exists(bswap_32 "byteswap.h" HAVE_DECL_BSWAP_32)
check_symbol_exists(bswap_64 "byteswap.h" HAVE_DECL_BSWAP_64)

# sys/select.h and sys/prctl.h headers
check_include_files("sys/select.h" HAVE_SYS_SELECT_H)
check_include_files("sys/prctl.h" HAVE_SYS_PRCTL_H)

# Bitmanip intrinsics
check_symbol_exists(__builtin_clz "" HAVE_DECL___BUILTIN_CLZ)
check_symbol_exists(__builtin_clzl "" HAVE_DECL___BUILTIN_CLZL)
check_symbol_exists(__builtin_clzll "" HAVE_DECL___BUILTIN_CLZLL)
check_symbol_exists(__builtin_popcount "" HAVE_DECL___BUILTIN_POPCOUNT)

# Memory management capabilities
check_symbol_exists(M_ARENA_MAX "malloc.h" HAVE_MALLOPT_ARENA_MAX)
check_symbol_exists(malloc_info "malloc.h" HAVE_MALLOC_INFO)

# Various system libraries
check_symbol_exists(strnlen "string.h" HAVE_DECL_STRNLEN)
check_symbol_exists(daemon "unistd.h" HAVE_DECL_DAEMON)

# Check for ways to obtain entropy
check_symbol_exists(getentropy "unistd.h" HAVE_GETENTROPY)
check_symbol_exists(getentropy "sys/random.h" HAVE_GETENTROPY_RAND)

check_cxx_source_compiles("
	#include <unistd.h>  /* for syscall */
	#include <sys/syscall.h>  /* for SYS_getrandom */
	int main() {
		syscall(SYS_getrandom, nullptr, 0, 0);
		return 0;
	}
" HAVE_SYS_GETRANDOM)

check_cxx_source_compiles("
	#include <sys/types.h>
	#include <sys/sysctl.h>
	int main() {
		static const int name[2] = {CTL_KERN, KERN_ARND};
		sysctl(name, 2, nullptr, nullptr, nullptr, 0);
		return 0;
	}
" HAVE_SYSCTL_ARND)

# Try to find libqrencode
 # Only used in the wallet GUI
 if(ENABLE_QRCODE AND ENABLE_WALLET AND ENABLE_QT)
 	find_package(QREncode REQUIRED)
 	set(USE_QRCODE 1 CACHE INTERNAL "QR code is enabled")
 endif()

configure_file(${PROJECT_SOURCE_DIR}/cmake/tapyrus-build-config.h.in tapyrus-config.h USE_SOURCE_PERMISSIONS @ONLY)

if(ENABLE_ZMQ)
	set(ENABLE_ZMQ 1)
else()
	set(ENABLE_ZMQ 0)
endif()
