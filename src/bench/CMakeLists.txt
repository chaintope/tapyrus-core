# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

add_executable(bench_tapyrus
  bench_tapyrus.cpp
  bench.cpp
# Benchmarks:
  base58.cpp
  bech32.cpp
  block_assemble.cpp
  ccoins_caching.cpp
  checkblock.cpp
  checkqueue.cpp
  crypto_hash.cpp
  examples.cpp
  lockedpool.cpp
  mempool_eviction.cpp
  merkle_root.cpp
  prevector.cpp
  rollingbloom.cpp
  verify_script.cpp
)

include(TargetDataSources)
target_raw_data_sources(bench_tapyrus NAMESPACE benchmark::data
  data/block413567.raw
)

target_link_libraries(bench_tapyrus
  core_interface
  test_util
  tapyrus_server
  Boost::headers
)

if(ENABLE_WALLET)
  target_sources(bench_tapyrus
    PRIVATE
      coin_selection.cpp
  )
  target_link_libraries(bench_tapyrus tapyrus_wallet)
endif()

add_test(NAME bench_sanity_check_high_priority
  COMMAND bench_tapyrus -sanity-check -priority-level=high
)

install_binary_component(bench_tapyrus)
