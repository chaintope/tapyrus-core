# Copyright (c) 2023-present The tapyrus Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
#
# Library dependency graph, laid out by hop distance from the executables
# (each tier is "one more link_libraries() hop away" from the top, not a
# conceptual layer), except tapyrus_consensus and tapyrus_crypto are kept
# in the base tier with tapyrus_clientversion regardless of hop count --
# they're genuinely foundational (tapyrus_crypto has no tapyrus_*
# dependencies at all; tapyrus_consensus depends only on tapyrus_crypto)
# and a couple of executables/libraries happen to reach them directly.
# test/lint/lint-library-layering.py enforces that nothing in a lower tier
# ever includes a header from a library in a tier above it, i.e. this
# diagram never gains a reverse arrow. A tier is a set, not a chain: it
# does not mean every entry in it links every entry in the tier below,
# only that none of them reach any higher.
#
#   tapyrusd  tapyrus-qt  test_tapyrus  bench_tapyrus  tapyrus-genesis  tapyrus-tx  tapyrus-cli  tapyrus-verify
#   ----------------------------------------------------------------------------------------------
#   tapyrus_chainstate  tapyrus_peer  tapyrus_rpc  tapyrus_wallet  tapyrus_cli  tapyrus_codec  tapyrus_scripting  tapyrus_global
#   ----------------------------------------------------------------------------------------------
#   tapyrus_netbase  tapyrus_util
#   ----------------------------------------------------------------------------------------------
#   tapyrus_consensus  tapyrus_crypto  tapyrus_clientversion
#
# The exact edges (who links whom) are not readable off the diagram above
# -- with this much fan-out, per-arrow lines would misrepresent it more
# than they'd clarify it. They're listed here instead.
#
# Per-executable direct library links:
#   tapyrusd, tapyrus-qt   -- tapyrus_chainstate, tapyrus_peer, tapyrus_rpc, tapyrus_wallet (cond., ENABLE_WALLET)
#   test_tapyrus           -- tapyrus_cli, tapyrus_chainstate, tapyrus_peer, tapyrus_rpc,
#                              tapyrus_wallet (cond.), tapyrus_codec, tapyrus_scripting
#   bench_tapyrus          -- tapyrus_chainstate, tapyrus_codec, tapyrus_wallet (cond.)
#   tapyrus-genesis        -- tapyrus_chainstate, tapyrus_codec (needs CheckBlock() from chainstate)
#   tapyrus-tx             -- tapyrus_codec, tapyrus_scripting
#   tapyrus-cli            -- tapyrus_cli, tapyrus_global
#   tapyrus-verify         -- tapyrus_consensus, tapyrus_global (no chain-state, no leveldb)
#
# Per-library direct dependencies:
#   tapyrus_rpc         -- tapyrus_wallet (cond.), tapyrus_peer, tapyrus_chainstate, tapyrus_codec
#   tapyrus_wallet      -- tapyrus_chainstate, tapyrus_peer, tapyrus_codec, tapyrus_scripting, tapyrus_global
#   tapyrus_peer        -- tapyrus_chainstate, tapyrus_netbase, tapyrus_scripting
#   tapyrus_chainstate  -- tapyrus_codec, tapyrus_netbase, tapyrus_scripting, tapyrus_global
#   tapyrus_codec       -- tapyrus_scripting, tapyrus_global
#   tapyrus_scripting   -- tapyrus_consensus, tapyrus_global, tapyrus_util
#   tapyrus_global      -- tapyrus_consensus, tapyrus_util
#   tapyrus_netbase     -- tapyrus_consensus, tapyrus_util
#   tapyrus_cli         -- tapyrus_consensus
#   tapyrus_util        -- tapyrus_clientversion, tapyrus_crypto
#   tapyrus_consensus   -- tapyrus_crypto
#
# The one edge that looks backwards at first glance: tapyrus_rpc depends
# on tapyrus_wallet, not the other way around. Three files that physically
# live in src/wallet/ compile as part of tapyrus_rpc instead, precisely
# because each one calls something only tapyrus_rpc has:
#   - wallet/rpcwallet.cpp touches CRPCTable directly to register the
#     wallet's RPC commands into the global table, and its own command
#     table extern-declares and registers...
#   - wallet/rpcdump.cpp's handlers (importprivkey, dumpwallet, etc.) --
#     pure RPC-command-handler content, not general wallet library code.
#   - wallet/walletinit_rpc.cpp, which holds only WalletInit::RegisterRPC()
#     (declared in wallet/walletinit.h, defined here because it calls
#     RegisterWalletRPCCommands()) -- every other WalletInit method stays
#     in wallet/init.cpp, in tapyrus_wallet.
# Every other wallet/*.cpp file only *implements* wallet behavior invoked
# through interfaces (CWallet methods, the RPC table's function pointers),
# so tapyrus_wallet itself never needs to look back up at tapyrus_rpc.
# That's what keeps the whole graph acyclic.

include(AddWindowsResources)

configure_file(${PROJECT_SOURCE_DIR}/cmake/tapyrus-build-config.h.in tapyrus-config.h USE_SOURCE_PERMISSIONS @ONLY)
if(WIN32)
  configure_windows_rc(tapyrusd         "tapyrusd (Tapyrus node with a JSON-RPC server)"              TAPYRUSD_RC_FILE)
  configure_windows_rc(tapyrus-cli      "tapyrus-cli (JSON-RPC client for ${PACKAGE_NAME})"           TAPYRUS_CLI_RC_FILE)
  configure_windows_rc(tapyrus-tx       "tapyrus-tx (CLI Tapyrus transaction editor utility)"         TAPYRUS_TX_RC_FILE)
  configure_windows_rc(tapyrus-genesis  "tapyrus-genesis (CLI Tapyrus genesis block generate utility)" TAPYRUS_GENESIS_RC_FILE)
endif()
include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

include(AddBoostIfNeeded)
add_boost_if_needed()

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
  COMMAND ${CMAKE_COMMAND} -DBUILD_INFO_HEADER_PATH=${PROJECT_BINARY_DIR}/src/tapyrus-build-info.h -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/script/GenerateBuildInfo.cmake
  COMMENT "Generating tapyrus-build-info.h"
  VERBATIM
)
add_library(tapyrus_clientversion STATIC EXCLUDE_FROM_ALL
  clientversion.cpp
)
target_compile_definitions(tapyrus_clientversion
  PRIVATE
    HAVE_BUILD_INFO
)
target_include_directories(tapyrus_clientversion
  PRIVATE
    ${PROJECT_BINARY_DIR}/src
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
        PUBLIC
        tapyrus_clientversion
        PRIVATE
        core_interface
        tapyrus_crypto
        univalue
        Boost::headers
        $<TARGET_NAME_IF_EXISTS:tapyrus_wallet>
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

# The mode-only static chain identity (ChainParamsBase) and per-deployment
# runtime federation identity (CFederationParams: networkId, message-start
# bytes, DNS seeds, softfork registration). Deliberately excludes genesis
# block creation/loading (see tapyrus_codec's genesisblock.cpp and
# tapyrus_scripting's dynamicparams.cpp, which hold the CDynamicParams
# genesis lifecycle) -- that split is what breaks a real circular
# dependency FederationParams used to have on key_io.cpp.
add_library(tapyrus_global STATIC EXCLUDE_FROM_ALL
  chainparams.cpp
  federationparams.cpp
)
target_link_libraries(tapyrus_global
  PUBLIC
    tapyrus_consensus
    tapyrus_util
  PRIVATE
    core_interface
    Boost::headers
)

# Genuinely shared, no single theme: netaddress/netbase (network address
# types), protocol (P2P message framing), scheduler (generic timer
# utility), warnings (generic warning-state utility). Named for its two
# largest/most identity-defining files rather than reused from any
# previous library name, to avoid silently changing what an existing
# name means.
add_library(tapyrus_netbase STATIC EXCLUDE_FROM_ALL
  netaddress.cpp
  netbase.cpp
  protocol.cpp
  scheduler.cpp
  warnings.cpp
)
target_link_libraries(tapyrus_netbase
  PUBLIC
    tapyrus_consensus
    tapyrus_util
  PRIVATE
    core_interface
    Boost::headers
    $<$<PLATFORM_ID:Windows>:ws2_32>
)

# Scripting/consensus-adjacent state shared by different executables and
# libraries: keys, keystores, script signing/standardness/solvability,
# coins, pstt, the xfield history cache, output-type classification, and
# the dynamic (per-run) genesis-block/CXFieldHistory-seeding lifecycle
# (dynamicparams.cpp). xfieldhistory sits here -- not in tapyrus_chainstate
# -- because federationparams.cpp (tapyrus_global, a lower layer) already
# depends on it; its own consumers span every layer above this one.
add_library(tapyrus_scripting STATIC EXCLUDE_FROM_ALL
  coins.cpp
  compressor.cpp
  dynamicparams.cpp
  key.cpp
  keystore.cpp
  outputtype.cpp
  policy/feerate.cpp
  pstt.cpp
  script/ismine.cpp
  script/sign.cpp
  script/standard.cpp
  xfieldhistory.cpp
)
target_link_libraries(tapyrus_scripting
  PUBLIC
    tapyrus_consensus
    tapyrus_global
    tapyrus_util
  PRIVATE
    core_interface
    univalue
    secp256k1
    Boost::headers
)

# Human-readable/wire encodings layered above tapyrus_scripting: Base58/
# key_io address formatting, core JSON read/write, output descriptors, and
# genesis-block creation (genesisblock.cpp -- needs DecodeDestination(),
# a tapyrus_codec-tier concern, which is why it can't live alongside the
# rest of federationparams.cpp in tapyrus_global).
add_library(tapyrus_codec STATIC EXCLUDE_FROM_ALL
  base58.cpp
  core_read.cpp
  core_write.cpp
  genesisblock.cpp
  key_io.cpp
  script/descriptor.cpp
)
target_link_libraries(tapyrus_codec
  PUBLIC
    tapyrus_scripting
    tapyrus_global
  PRIVATE
    core_interface
    tapyrus_util
    univalue
    secp256k1
    Boost::headers
)

include(InstallBinaryComponent)

# Blockchain/mempool consensus-application state: chain index, UTXO set,
# block/tx validation, mempool, block-storage indexes (leveldb-backed).
add_library(tapyrus_chainstate STATIC EXCLUDE_FROM_ALL
  blockprune.cpp
  chain.cpp
  chainstate.cpp
  checkpoints.cpp
  consensus/tx_verify.cpp
  cs_main.cpp
  dbwrapper.cpp
  index/base.cpp
  index/txindex.cpp
  issuedcolorids.cpp
  miner.cpp
  policy/fees.cpp
  policy/packages.cpp
  policy/policy.cpp
  policy/rbf.cpp
  script/sigcache.cpp
  txdb.cpp
  txmempool.cpp
  utxo_snapshot.cpp
  validation.cpp
  validationinterface.cpp
  verifydb.cpp
)
target_link_libraries(tapyrus_chainstate
  PUBLIC
    tapyrus_global
    tapyrus_scripting
    tapyrus_util
    Boost::headers
  PRIVATE
    core_interface
    tapyrus_codec
    tapyrus_netbase
    leveldb
)
target_include_directories(tapyrus_chainstate
  PRIVATE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/leveldb/helpers/memenv>
)

# Peer-to-peer networking: connection management, message processing,
# address manager/database, Tor control, bloom filters, and the compact
# block/merkle-block relay encodings.
add_library(tapyrus_peer STATIC EXCLUDE_FROM_ALL
  addrdb.cpp
  addrman.cpp
  bloom.cpp
  blockencodings.cpp
  merkleblock.cpp
  net.cpp
  net_processing.cpp
  torcontrol.cpp
)
target_link_libraries(tapyrus_peer
  PUBLIC
    tapyrus_chainstate
    tapyrus_netbase
    tapyrus_scripting
    tapyrus_util
  PRIVATE
    core_interface
    Boost::headers
)

# tapyrus_wallet needs tapyrus_chainstate/tapyrus_peer/tapyrus_codec, so it
# can only be defined once those exist -- see the dependency graph and the
# wallet/rpcwallet.cpp note at the top of this file for why this doesn't
# create a cycle back from tapyrus_rpc.
if(ENABLE_WALLET)
  add_subdirectory(wallet)
endif()

# JSON-RPC and REST server functionality used by `tapyrusd` and
# `tapyrus-qt` executables. wallet/rpcwallet.cpp and wallet/rpcdump.cpp
# compile here rather than into tapyrus_wallet -- both are pure RPC-command-
# handler content (rpcdump.cpp's handlers are extern-declared and registered
# by rpcwallet.cpp's own command table). wallet/walletinit_rpc.cpp compiles
# here too: it holds the one WalletInit method (RegisterRPC) that has to
# call RegisterWalletRPCCommands(), so that the rest of WalletInit -- which
# stays in wallet/init.cpp, in tapyrus_wallet -- doesn't drag tapyrus_rpc
# down with it. See the note at the top of this file.
set(TAPYRUS_RPC_SOURCES
  httprpc.cpp
  httpserver.cpp
  rest.cpp
  rpc/blockchain.cpp
  rpc/mempool.cpp
  rpc/mining.cpp
  rpc/misc.cpp
  rpc/net.cpp
  rpc/rawtransaction.cpp
  rpc/server.cpp
  rpc/util.cpp
)
if(ENABLE_WALLET)
  list(APPEND TAPYRUS_RPC_SOURCES
    wallet/rpcdump.cpp
    wallet/rpcwallet.cpp
    wallet/walletinit_rpc.cpp
  )
endif()
add_library(tapyrus_rpc STATIC EXCLUDE_FROM_ALL ${TAPYRUS_RPC_SOURCES})
target_link_libraries(tapyrus_rpc
  PUBLIC
    tapyrus_scripting
    univalue
  PRIVATE
    core_interface
    tapyrus_chainstate
    tapyrus_codec
    tapyrus_netbase
    tapyrus_peer
    tapyrus_util
    $<TARGET_NAME_IF_EXISTS:tapyrus_wallet>
    $<TARGET_NAME_IF_EXISTS:libevent::extra>
    $<TARGET_NAME_IF_EXISTS:libevent::core>
    $<TARGET_NAME_IF_EXISTS:libevent::pthreads>
    # rest.cpp/rpc/blockchain.cpp/rpc/rawtransaction.cpp include
    # <index/txindex.h> -> <index/base.h> -> <dbwrapper.h> -> <leveldb/db.h>
    # directly; tapyrus_chainstate links leveldb PRIVATE, so that include
    # path doesn't propagate here on its own -- link leveldb directly to
    # get it, same as test_tapyrus and bench_tapyrus do.
    leveldb
    Boost::headers
)


# tapyrus Core tapyrusd. The six files below are orchestration/glue: they
# wire the split libraries together at startup/shutdown and have no
# meaningful reuse outside this one executable, so they compile directly
# into tapyrusd rather than into a library of their own.
if(BUILD_DAEMON)
  add_executable(tapyrusd
    tapyrusd.cpp
    file_io.cpp
    init.cpp
    noui.cpp
    shutdown.cpp
    timeoffsets.cpp
    ui_interface.cpp
  )
  add_windows_resources(tapyrusd "${TAPYRUSD_RC_FILE}")
  target_link_libraries(tapyrusd PRIVATE
    core_interface
    tapyrus_chainstate
    tapyrus_peer
    tapyrus_rpc
    tapyrus_util
    $<TARGET_NAME_IF_EXISTS:tapyrus_zmq>
    $<TARGET_NAME_IF_EXISTS:tapyrus_wallet>
    univalue
    # file_io.cpp/init.cpp reach <dbwrapper.h> -> <leveldb/db.h> the same
    # way tapyrus_rpc's rest.cpp/etc. do -- see the comment on tapyrus_rpc's
    # target_link_libraries above.
    leveldb
    Boost::headers
  )
  install_binary_component(tapyrusd HAS_MANPAGE)

  target_include_directories(tapyrusd
          PRIVATE
          ${PROJECT_SOURCE_DIR}/src/univalue/include
  )

  # Conditionally include ZMQ include directory if it exists (used only by
  # init.cpp's ZMQ notification wiring).
  if(ZMQ_INCLUDE_DIR)
    target_include_directories(tapyrusd
      PRIVATE
        ${ZMQ_INCLUDE_DIR}
    )
  endif()
endif()


add_library(tapyrus_cli STATIC EXCLUDE_FROM_ALL
  rpc/client.cpp
)
target_link_libraries(tapyrus_cli
  # rpc/client.h (the public header) exposes UniValue in its own function
  # signatures, so univalue has to stay PUBLIC. Nothing here needs
  # tapyrus_consensus or Boost -- rpc/client.cpp uses neither.
  PUBLIC
    univalue
  PRIVATE
    core_interface
)


# tapyrus Core RPC client. Only needs the mode/network identity
# (tapyrus_global) and its own request-building/formatting code
# (tapyrus_cli, tapyrus_util) -- no scripting, codec, or chain-state tier.
if(BUILD_CLI)
  add_executable(tapyrus-cli tapyrus-cli.cpp)
  add_windows_resources(tapyrus-cli "${TAPYRUS_CLI_RC_FILE}")
  target_link_libraries(tapyrus-cli
    PRIVATE
    core_interface
    tapyrus_cli
    tapyrus_global
    tapyrus_util
    libevent::extra
    libevent::core
    Boost::headers
  )
  install_binary_component(tapyrus-cli HAS_MANPAGE)
endif()


# Transaction editor. Needs scripting (keys/signing) and codec (address
# encoding) but not chain-state -- it never validates against the chain.
if(BUILD_UTILS)
  add_executable(tapyrus-tx tapyrus-tx.cpp)
  add_windows_resources(tapyrus-tx "${TAPYRUS_TX_RC_FILE}")
  target_link_libraries(tapyrus-tx
    PRIVATE
    core_interface
    tapyrus_codec
    tapyrus_scripting
    tapyrus_util
    univalue
    Boost::headers
  )
  install_binary_component(tapyrus-tx HAS_MANPAGE)
endif()

# Needs tapyrus_chainstate too (not just codec) because it calls
# CheckBlock() on the block it just built, in validation.cpp.
#
# shutdown.cpp/ui_interface.cpp are added for the same reason test_tapyrus
# needed them: index/base.cpp's BaseIndex::BlockConnected()/
# ChainStateFlushed() call StartShutdown()/uiInterface directly on a fatal
# index error, and tapyrus_chainstate links neither -- see the six-glue-
# files comment on tapyrusd's add_executable() above. Unlike test_tapyrus,
# this executable has no test-harness stand-ins for these, so it needs the
# real files.
if(BUILD_GENESIS)
  add_executable(tapyrus-genesis tapyrus-genesis.cpp shutdown.cpp ui_interface.cpp)
  add_windows_resources(tapyrus-genesis "${TAPYRUS_GENESIS_RC_FILE}")
  target_link_libraries(tapyrus-genesis
          PRIVATE
          core_interface
          tapyrus_chainstate
          tapyrus_codec
          tapyrus_util
          Boost::headers
  )
  install_binary_component(tapyrus-genesis HAS_MANPAGE)
endif()

# No tapyrus_chainstate, no leveldb: transaction/script-level only.
# secp256k1/tapyrus_crypto/univalue are not listed -- tapyrus-verify.cpp
# calls nothing from them directly, they're purely implementation details
# of tapyrus_consensus/tapyrus_global reached transitively at link time.
if(BUILD_SCRIPT_VERIFY)
  add_executable(tapyrus-verify tapyrus-verify.cpp)
  target_link_libraries(tapyrus-verify
          PRIVATE
          core_interface
          tapyrus_consensus
          tapyrus_global
          Boost::headers
  )
  install_binary_component(tapyrus-verify)
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
