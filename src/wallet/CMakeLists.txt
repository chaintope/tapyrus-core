# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Add Berkeley DB dependency.
include(FindBerkeleyDB)
find_package(BerkeleyDB REQUIRED)

add_library(tapyrus_wallet STATIC
        coincontrol.cpp
        coinselection.cpp
        crypter.cpp
        db.cpp
        feebumper.cpp
        fees.cpp
        init.cpp
        ../interfaces/wallet.cpp
        rpcdump.cpp
        rpcwallet.cpp
        wallet.cpp
        walletdb.cpp
        walletutil.cpp
)
target_link_libraries(tapyrus_wallet
  PRIVATE
    core_interface
<<<<<<< HEAD
=======
    tapyrus_common
    tapyrus_util
>>>>>>> 9bc7d17b8c (port cmake from bitcoin to tapyrus; make build settings uniform across both builds. fix include path of tapyrus-config, test data herder format etc)
    tapyrus_server
    univalue
    ${BerkeleyDB_LIBRARY}
    Boost::headers
    Boost::system
    $<TARGET_NAME_IF_EXISTS:USDT::headers>
)
message(STATUS "BerkeleyDB_INCLUDE_DIR: ${BerkeleyDB_INCLUDE_DIR}")
target_include_directories(tapyrus_wallet PUBLIC ${BerkeleyDB_INCLUDE_DIR})