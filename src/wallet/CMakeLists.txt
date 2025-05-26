# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Add Berkeley DB dependency.
find_package(BerkeleyDB REQUIRED)

add_library(tapyrus_wallet
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
        ../outputtype.cpp
        ../script/ismine.cpp
)
target_link_libraries(tapyrus_wallet
  PRIVATE
    core_interface
    tapyrus_util
    tapyrus_common
    tapyrus_server
    univalue
    ${BerkeleyDB_LIBRARY}
    Boost::headers
    $<TARGET_NAME_IF_EXISTS:USDT::headers>
)

target_include_directories(tapyrus_wallet PUBLIC ${BerkeleyDB_INCLUDE_DIR})