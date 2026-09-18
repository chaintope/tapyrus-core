# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

add_executable(bench_tapyrus
  bench_tapyrus.cpp
  bench.cpp
  # Glue -- see the six-glue-files comment on tapyrusd's add_executable()
  # in the top-level src/CMakeLists.txt. checkblock.cpp/block_assemble.cpp
  # exercise tapyrus_chainstate code paths (CChainState::ActivateBestChain,
  # BaseIndex::BlockConnected, AbortNode, etc.) that call
  # StartShutdown()/ShutdownRequested()/uiInterface/SaveBlockToDisk/
  # ReadBlockFromDisk/FlushStateToDisk/OpenUndoFile directly -- only these
  # three are needed here (unlike test_tapyrus/tapyrusd, bench never hits
  # noui.cpp's or timeoffsets.cpp's symbols).
  ../file_io.cpp
  ../shutdown.cpp
  ../ui_interface.cpp
# Benchmarks:
  base58.cpp
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
  PRIVATE
  core_interface
  tapyrus_chainstate
  tapyrus_peer
  tapyrus_codec
  # block_assemble.cpp includes <txdb.h> -> <dbwrapper.h> -> <leveldb/db.h>
  # directly; tapyrus_chainstate links leveldb PRIVATE, so that include path
  # doesn't propagate here on its own -- link leveldb directly to get it,
  # same as test_tapyrus does.
  leveldb
  Boost::headers
)

if(ENABLE_WALLET)
  target_sources(bench_tapyrus
    PRIVATE
      coin_selection.cpp
  )
  target_link_libraries(bench_tapyrus PRIVATE tapyrus_wallet)
endif()

install_binary_component(bench_tapyrus)
