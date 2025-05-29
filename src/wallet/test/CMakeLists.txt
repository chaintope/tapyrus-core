# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Do not use generator expressions in test sources because the
# SOURCES property is processed to gather test suite macros.
target_sources(test_tapyrus
  PRIVATE
    wallet_test_fixture.cpp
    coinselector_tests.cpp
    psbt_wallet_tests.cpp
    wallet_crypto_tests.cpp
    wallet_tests.cpp
)
target_link_libraries(test_tapyrus tapyrus_wallet)
