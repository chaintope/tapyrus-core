# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Exclude secp256k1 tests from CTest
# These tests are not built (SECP256K1_BUILD_TESTS=OFF) and are tested
# separately by the secp256k1 library's own test suite
if(TEST test_secp256k1)
  set_tests_properties(test_secp256k1 PROPERTIES DISABLED TRUE)
endif()
if(TEST exhaustive_test_secp256k1)
  set_tests_properties(exhaustive_test_secp256k1 PROPERTIES DISABLED TRUE)
endif()

if(TARGET tapyrus-util AND TARGET tapyrus-tx AND TARGET Python3::Interpreter)
  add_test(NAME util_test_runner
    COMMAND ${CMAKE_COMMAND} -E env BITCOINUTIL=$<TARGET_FILE:tapyrus-util> BITCOINTX=$<TARGET_FILE:tapyrus-tx> Python3::Interpreter ${PROJECT_BINARY_DIR}/test/util/test_runner.py
  )
endif()

if(TARGET Python3::Interpreter)
  add_test(NAME util_rpcauth_test
    COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/test/util/rpcauth-test.py
  )
endif()
