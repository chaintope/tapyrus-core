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
  tapyrus_server
  tapyrus_common
  tapyrus_consensus
  $<TARGET_NAME_IF_EXISTS:tapyrus_zmq>
  Boost::headers
  $<TARGET_NAME_IF_EXISTS:libevent::extra>
  $<TARGET_NAME_IF_EXISTS:libevent::core>
  $<TARGET_NAME_IF_EXISTS:libevent::pthreads>
)
target_include_directories(bench_tapyrus
  PRIVATE
    ${BerkeleyDB_INCLUDE_DIR}
    ${EVENT_INCLUDE_DIRS}
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../leveldb>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../leveldb/include>
)

if(ENABLE_WALLET)
  target_sources(bench_tapyrus
    PRIVATE
      coin_selection.cpp
  )
  target_link_libraries(bench_tapyrus tapyrus_wallet)
endif()

add_test(NAME bench_sanity_check
  COMMAND bench_tapyrus -sanity-check
)

install_binary_component(bench_tapyrus)
