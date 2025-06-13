# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

add_library(tapyrus_crypto STATIC EXCLUDE_FROM_ALL
  aes.cpp
  chacha20.cpp
  hmac_sha256.cpp
  hmac_sha512.cpp
  ripemd160.cpp
  sha1.cpp
  sha256.cpp
  sha256_sse4.cpp
  sha512.cpp
  ../support/cleanse.cpp
)

target_link_libraries(tapyrus_crypto
  PRIVATE
    core_interface
)

if(HAVE_SSE41)
  target_compile_definitions(tapyrus_crypto PRIVATE ENABLE_SSE41)
  target_sources(tapyrus_crypto PRIVATE sha256_sse41.cpp)
  set_property(SOURCE sha256_sse41.cpp PROPERTY
    COMPILE_OPTIONS ${SSE41_CXXFLAGS}
  )
endif()

if(HAVE_AVX2)
  target_compile_definitions(tapyrus_crypto PRIVATE ENABLE_AVX2)
  target_sources(tapyrus_crypto PRIVATE sha256_avx2.cpp)
  set_property(SOURCE sha256_avx2.cpp PROPERTY
    COMPILE_OPTIONS ${AVX2_CXXFLAGS}
  )
endif()
#
#if(HAVE_SSE41 AND HAVE_X86_SHANI)
#  target_compile_definitions(tapyrus_crypto PRIVATE ENABLE_SSE41 ENABLE_X86_SHANI)
#  target_sources(tapyrus_crypto PRIVATE sha256_x86_shani.cpp)
#  set_property(SOURCE sha256_x86_shani.cpp PROPERTY
#    COMPILE_OPTIONS ${X86_SHANI_CXXFLAGS}
#  )
#endif()

#if(HAVE_ARM_SHANI)
#  target_compile_definitions(tapyrus_crypto PRIVATE ENABLE_ARM_SHANI)
#  target_sources(tapyrus_crypto PRIVATE sha256_arm_shani.cpp)
#  set_property(SOURCE sha256_arm_shani.cpp PROPERTY
#    COMPILE_OPTIONS ${ARM_SHANI_CXXFLAGS}
#  )
#endif()
