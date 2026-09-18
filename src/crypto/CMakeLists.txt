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

# Checked via the HAVE_* capability-probe results directly (matches
# upstream Bitcoin Core's src/crypto/CMakeLists.txt exactly). tapyrus used
# to also alias these into ENABLE_* CMake variables purely to feed
# tapyrus-config.h's ENABLE_ARM_SHANI/AVX2/SSE41/X86_SHANI entries -- upstream's
# own config header never had those entries at all, and nothing in
# src/crypto/ includes tapyrus-config.h, so that alias and the header
# entries it fed were dead weight. Removed both; target_compile_definitions
# below is the only thing that ever needed to exist.
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

if(HAVE_X86_SHANI)
  target_compile_definitions(tapyrus_crypto PRIVATE ENABLE_SHANI)
  target_sources(tapyrus_crypto PRIVATE sha256_shani.cpp)
  set_property(SOURCE sha256_shani.cpp PROPERTY
    COMPILE_OPTIONS ${X86_SHANI_CXXFLAGS}
  )
endif()

# ARM SHA-NI: no source file (sha256_arm_shani.cpp) exists yet.
# Uncomment when ARM SHA-NI support is ported.
#if(HAVE_ARM_SHANI)
#  target_compile_definitions(tapyrus_crypto PRIVATE ENABLE_ARM_SHANI)
#  target_sources(tapyrus_crypto PRIVATE sha256_arm_shani.cpp)
#  set_property(SOURCE sha256_arm_shani.cpp PROPERTY
#    COMPILE_OPTIONS ${ARM_SHANI_CXXFLAGS}
#  )
#endif()
