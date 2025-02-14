# Copyright (c) 2018 The Bitcoin developers


# Ensure that WINDRES_PREPROC is enabled when using windres.
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    list(APPEND CMAKE_RC_FLAGS "-DWINDRES_PREPROC")
endif()

# Enable warning
include(AddCompilerFlags)

add_c_compiler_flag(-Wnested-externs -Wstrict-prototypes)
add_compiler_flag(
        -Wall
        -Wextra
        -Wformat
        -Wvla
        -Wformat-security
        -Wcast-align
        -Wunused-parameter
        -Wmissing-braces
        # FIXME: Activating this flag cause cmake to fail on leveldb.
        # -Wthread-safety-analysis
        -Wshadow
)

option(EXTRA_WARNINGS "Enable extra warnings" OFF)
if(EXTRA_WARNINGS)
    add_cxx_compiler_flag(-Wsuggest-override)
else()
    add_compiler_flag(-Wno-unused-parameter)
endif()


# libtool style configure
add_subdirectory(config)

# libraries
add_subdirectory(crypto)
add_subdirectory(leveldb)
add_subdirectory(secp256k1)
add_subdirectory(univalue)

# Because the Bitcoin ABc source code is disorganised, we
# end up with a bunch of libraries without any aparent
# cohesive structure. This is inherited from Bitcoin Core
# and reflecting this.
# TODO: Improve the structure once cmake is rocking.

# Various completely unrelated features shared by all executables.
add_library(util
        clientversion.cpp
        fs.cpp
        logging.cpp
        random.cpp
        randomenv.cpp
        rpc/protocol.cpp
        support/cleanse.cpp
        support/lockedpool.cpp
        sync.cpp
        threadinterrupt.cpp
        uint256.cpp
        util.cpp
        utilmoneystr.cpp
        utilstrencodings.cpp
        utiltime.cpp
        )

target_compile_definitions(util PUBLIC HAVE_CONFIG_H)
target_include_directories(util
        PUBLIC
        .
        # To access the config.
        ${CMAKE_CURRENT_BINARY_DIR}
        )

# Target specific configs
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    find_package(SHLWAPI REQUIRED)
    target_link_libraries(util ${SHLWAPI_LIBRARY})
    target_include_directories(util PUBLIC ${SHLWAPI_INCLUDE_DIR})

    find_library(WS2_32_LIBRARY NAMES ws2_32)
    target_link_libraries(util ${WS2_32_LIBRARY})
endif()

# Boost packages
set(BOOST_PACKAGES_REQUIRED filesystem)

function(prepend var prefix)
    set(listVar "")
    foreach(f ${ARGN})
        list(APPEND listVar "${prefix}${f}")
    endforeach(f)
    set(${var} "${listVar}" PARENT_SCOPE)
endfunction(prepend)

prepend(BOOST_LIBRARIES "Boost::" ${BOOST_PACKAGES_REQUIRED})

find_package(Boost 1.81 REQUIRED ${BOOST_PACKAGES_REQUIRED})
target_link_libraries(util univalue crypto ${BOOST_LIBRARIES})

# Make sure boost uses std::atomic (it doesn't before 1.63)
target_compile_definitions(util PUBLIC BOOST_SP_USE_STD_ATOMIC BOOST_AC_USE_STD_ATOMIC)

# More completely unrelated features shared by all executables.
# Because nothing says this is different from util than "common"
add_library(common
        base58.cpp
        chainparams.cpp
        coins.cpp
        compressor.cpp
        core_read.cpp
        core_write.cpp
        federationparams.cpp
        key.cpp
        keystore.cpp
        netaddress.cpp
        netbase.cpp
        protocol.cpp
        scheduler.cpp
        script/sign.cpp
        script/standard.cpp
        warnings.cpp
        key_io.cpp
        bench/bech32.cpp
        bech32.cpp
        coloridentifier.cpp
        xfieldhistory.cpp
        )

target_link_libraries(common util secp256k1 tapyrusconsensus)

# libtapyrusconsensus
add_library(tapyrusconsensus
        arith_uint256.cpp
        consensus/merkle.cpp
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
target_link_libraries(tapyrusconsensus common)

# Tapyrus server facilities
add_library(server
        addrman.cpp
        addrdb.cpp
        bloom.cpp
        blockencodings.cpp
        chain.cpp
        checkpoints.cpp
        consensus/tx_verify.cpp
        httprpc.cpp
        httpserver.cpp
        init.cpp
        interfaces/handler.cpp
        interfaces/node.cpp
        dbwrapper.cpp
        merkleblock.cpp
        miner.cpp
        net.cpp
        net_processing.cpp
        noui.cpp
        policy/fees.cpp
        policy/feerate.cpp
        policy/policy.cpp
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
#        script/scriptcache.cpp
        script/descriptor.cpp
        script/sigcache.cpp
        script/ismine.cpp
        timeoffsets.cpp
        torcontrol.cpp
        txdb.cpp
        txmempool.cpp
        ui_interface.cpp
        utxo_snapshot.cpp
        validation.cpp
        validationinterface.cpp
        key_io.cpp
        shutdown.cpp
        outputtype.cpp
        zmq/zmqrpc.cpp
        bench/bech32.cpp
        bech32.cpp
        index/txindex.cpp
        index/base.cpp
        )

# This require libevent
find_package(Event REQUIRED)
find_package(UPNP OPTIONAL_COMPONENTS)

target_link_libraries(server
        Event
        tapyrusconsensus
        leveldb
        )

if(UPNP_FOUND)
    target_link_libraries(server
            ${UPNP_LIBRARY}
    )
endif()

target_include_directories(common
        PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/leveldb/include>
)
target_include_directories(server PUBLIC leveldb/helpers/memenv)

# Test suite.
if(BUILD_TEST)
    add_subdirectory(test)
endif()

# Benchmark suite.
if(BUILD_BENCH)
    add_subdirectory(bench)
endif()

# Wallet
if(BUILD_TAPYRUS_WALLET)
    add_subdirectory(wallet)
    target_link_libraries(server wallet)
endif()

# ZeroMQ
if(BUILD_TAPYRUS_ZMQ)
    add_subdirectory(zmq)
    target_link_libraries(server zmq)
endif()

# RPC client support
add_library(rpcclient rpc/client.cpp)
target_link_libraries(rpcclient univalue util)

# tapyrus-cli
if(BUILD_TAPYRUS_CLI)
    add_executable(tapyrus-cli tapyrus-cli.cpp)
    if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
        target_sources(tapyrus-cli PRIVATE tapyrus-cli-res.rc)
    endif()

    target_link_libraries(tapyrus-cli tapyrusconsensus common rpcclient Event)
endif()

# tapyrus-tx
if(BUILD_TAPYRUS_TX)
    add_executable(tapyrus-tx tapyrus-tx.cpp)
    if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
        target_sources(tapyrus-tx PRIVATE tapyrus-tx-res.rc)
    endif()

    target_link_libraries(tapyrus-tx tapyrusconsensus)
endif()

# tapyrus-genesis
if(BUILD_TAPYRUS_GENESIS)
    add_executable(tapyrus-genesis tapyrus-genesis.cpp)
    if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
        target_sources(tapyrus-genesis PRIVATE tapyrus-genesis-res.rc)
    endif()

    target_link_libraries(tapyrus-genesis
            common
            tapyrusconsensus
            server
            )
endif()

# tapyrusd
add_executable(tapyrusd tapyrusd.cpp)
target_link_libraries(tapyrusd server)
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    target_sources(tapyrusd PRIVATE tapyrus-res.rc)
endif()

# Tapyrus-qt
if(BUILD_TAPYRUS_QT)
    add_subdirectory(qt)
endif()
