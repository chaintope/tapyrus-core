# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

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
