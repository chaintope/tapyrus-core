# Copyright (c) 2023-present The tapyrus Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

include(AddWindowsResources)

configure_file(${PROJECT_SOURCE_DIR}/cmake/tapyrus-build-config.h.in tapyrus-config.h USE_SOURCE_PERMISSIONS @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

#=============================
# Subprojects
#=============================
# Subprojects include subdirectories that do or could have tests
# and/or benchmark binaries, such as all subtrees and univalue.
# These need to be included before CMAKE_*_OUTPUT_DIRECTORY variables
# are set, so output locations of subproject tests and libraries are
# not overridden.
add_subdirectory(leveldb)
add_subdirectory(univalue)

# Set top-level target output locations.
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
endif()
if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
endif()
if(NOT CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
endif()

add_custom_target(generate_build_info
  BYPRODUCTS ${PROJECT_BINARY_DIR}/src/tapyrus-build-info.h
  COMMAND ${CMAKE_COMMAND} -DENABLE_INFO_HEADER_PATH=${PROJECT_BINARY_DIR}/src/tapyrus-build-info.h -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/script/GenerateBuildInfo.cmake
  COMMENT "Generating tapyrus-build-info.h"
  VERBATIM
)
add_library(tapyrus_clientversion STATIC EXCLUDE_FROM_ALL
  clientversion.cpp
)
target_link_libraries(tapyrus_clientversion
  PRIVATE
    core_interface
)
add_dependencies(tapyrus_clientversion generate_build_info)

add_subdirectory(crypto)

add_library(tapyrus_consensus STATIC EXCLUDE_FROM_ALL
  arith_uint256.cpp
  consensus/merkle.cpp
  coloridentifier.cpp
  hash.cpp
  primitives/block.cpp
  primitives/transaction.cpp
  primitives/xfield.cpp
  pubkey.cpp
  script/tapyrusconsensus.cpp
  script/interpreter.cpp
  script/script.cpp
  script/script_error.cpp
  uint256.cpp
  utilstrencodings.cpp
)

target_link_libraries(tapyrus_consensus
  PRIVATE
    core_interface
    tapyrus_crypto
    secp256k1
    Boost::headers
)

if(ENABLE_ZMQ)
  add_subdirectory(zmq)
endif()

add_library(tapyrus_util STATIC EXCLUDE_FROM_ALL
        clientversion.cpp
        fs.cpp
        interfaces/handler.cpp
        interfaces/node.cpp
        logging.cpp
        random.cpp
        randomenv.cpp
        rpc/protocol.cpp
        support/cleanse.cpp
        support/lockedpool.cpp
        sync.cpp
        threadinterrupt.cpp
        util.cpp
        utilmoneystr.cpp
        utilstrencodings.cpp
        utiltime.cpp
)
target_link_libraries(tapyrus_util
        PRIVATE
        univalue
        Boost::headers
)
target_compile_definitions(tapyrus_util PUBLIC HAVE_CONFIG_H)
target_include_directories(tapyrus_util
        PUBLIC
        .
        # To access the config.
        ${CMAKE_CURRENT_BINARY_DIR}
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/univalue/include>
        ${BerkeleyDB_INCLUDE_DIR}
)

# Home for common functionality shared by different executables and libraries.
# Similar to `tapyrus_util` library, but higher-level.
add_library(tapyrus_common STATIC EXCLUDE_FROM_ALL
  base58.cpp
  bech32.cpp
  chainparams.cpp
  coins.cpp
  compressor.cpp
  core_read.cpp
  core_write.cpp
  federationparams.cpp
  key.cpp
  keystore.cpp
  key_io.cpp
  netaddress.cpp
  netbase.cpp
  policy/feerate.cpp
  protocol.cpp
  scheduler.cpp
  script/descriptor.cpp
  script/ismine.cpp
  script/sign.cpp
  script/standard.cpp
  warnings.cpp
  xfieldhistory.cpp
)

target_link_libraries(tapyrus_common
  PRIVATE
    core_interface
    tapyrus_consensus
    tapyrus_util
    univalue
    secp256k1
    Boost::headers
    $<TARGET_NAME_IF_EXISTS:USDT::headers>
    $<$<PLATFORM_ID:Windows>:ws2_32>
)
target_include_directories(tapyrus_common
        PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/leveldb>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/leveldb/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/univalue/include>
)

include(InstallBinaryComponent)

if(ENABLE_WALLET)
  add_subdirectory(wallet)
endif()


# P2P and RPC server functionality used by `tapyrusd` and `tapyrus-qt` executables.
add_library(tapyrus_server STATIC EXCLUDE_FROM_ALL
  addrdb.cpp
  addrman.cpp
  bloom.cpp
  blockencodings.cpp
  blockprune.cpp
  chain.cpp
  chainstate.cpp
  checkpoints.cpp
  consensus/tx_verify.cpp
  cs_main.cpp
  dbwrapper.cpp
  file_io.cpp
  httprpc.cpp
  httpserver.cpp
  index/base.cpp
  index/txindex.cpp
  init.cpp
  merkleblock.cpp
  miner.cpp
  net.cpp
  net_processing.cpp
  noui.cpp
  outputtype.cpp
  policy/policy.cpp
  policy/fees.cpp
  policy/packages.cpp
  policy/rbf.cpp
  rest.cpp
  rpc/blockchain.cpp
  rpc/mempool.cpp
  rpc/mining.cpp
  rpc/misc.cpp
  rpc/net.cpp
  rpc/rawtransaction.cpp
  rpc/server.cpp
  rpc/util.cpp
  script/sigcache.cpp
  shutdown.cpp
  timeoffsets.cpp
  torcontrol.cpp
  txdb.cpp
  txmempool.cpp
  ui_interface.cpp
  utxo_snapshot.cpp
  validation.cpp
  validationinterface.cpp
  verifydb.cpp
)
target_link_libraries(tapyrus_server
  PRIVATE
    core_interface
    tapyrus_common
    tapyrus_util
    $<TARGET_NAME_IF_EXISTS:tapyrus_zmq>
    $<TARGET_NAME_IF_EXISTS:tapyrus_wallet>
    leveldb
    univalue
    Boost::headers
    $<TARGET_NAME_IF_EXISTS:libevent::core>
    $<TARGET_NAME_IF_EXISTS:libevent::extra>
    $<TARGET_NAME_IF_EXISTS:libevent::pthreads>
    $<TARGET_NAME_IF_EXISTS:USDT::headers>
)

target_include_directories(tapyrus_server
        PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/leveldb
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/leveldb/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/leveldb/helpers/memenv>
        ${BerkeleyDB_INCLUDE_DIR}
)

# Conditionally include ZMQ include directory if it exists
if(ZMQ_INCLUDE_DIR)
    target_include_directories(tapyrus_server
        PRIVATE
            ${ZMQ_INCLUDE_DIR}
    )
endif()


# tapyrus Core tapyrusd.
if(BUILD_DAEMON)
  add_executable(tapyrusd
    tapyrusd.cpp
  )
  add_windows_resources(tapyrusd tapyrusd-res.rc)
  target_link_libraries(tapyrusd PRIVATE
    Boost::headers
    $<TARGET_NAME_IF_EXISTS:tapyrus_wallet>
  )
  install_binary_component(tapyrusd HAS_MANPAGE)

  target_include_directories(tapyrusd
          PUBLIC
          ${PROJECT_SOURCE_DIR}/src/univalue/include
  )
endif()


add_library(tapyrus_cli STATIC EXCLUDE_FROM_ALL
  rpc/client.cpp
)
target_link_libraries(tapyrus_cli
  PUBLIC
    core_interface
    univalue
    tapyrus_consensus
    Boost::headers
)


# tapyrus Core RPC client
if(BUILD_CLI)
  add_executable(tapyrus-cli tapyrus-cli.cpp)
  add_windows_resources(tapyrus-cli tapyrus-cli-res.rc)
  target_link_libraries(tapyrus-cli
    core_interface
    tapyrus_cli
    tapyrus_util
    tapyrus_common
    libevent::core
    libevent::extra
    Boost::headers
  )
  install_binary_component(tapyrus-cli HAS_MANPAGE)
endif()


if(BUILD_UTILS)
  add_executable(tapyrus-tx tapyrus-tx.cpp)
  add_windows_resources(tapyrus-tx tapyrus-tx-res.rc)
  target_link_libraries(tapyrus-tx
    core_interface
    tapyrus_common
    tapyrus_util
    univalue
    Boost::headers
  )
  install_binary_component(tapyrus-tx HAS_MANPAGE)
endif()

if(BUILD_GENESIS)
  add_executable(tapyrus-genesis tapyrus-genesis.cpp)
  add_windows_resources(tapyrus-genesis tapyrus-genesis-res.rc)
  target_link_libraries(tapyrus-genesis
          core_interface
          tapyrus_server
          Boost::headers
  )
  install_binary_component(tapyrus-genesis HAS_MANPAGE)
endif()

if(BUILD_GUI)
  add_subdirectory(qt)
endif()


if(ENABLE_BENCH)
  add_subdirectory(bench)
endif()

if(ENABLE_TESTS)
  add_subdirectory(test)
endif()
