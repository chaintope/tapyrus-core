# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Add Berkeley DB dependency. Version range matches the top-level
# find_package(BerkeleyDB ...) call in CMakeLists.txt -- see the comment
# there for why 5.99 is a license ceiling, not a technical one.
include(FindBerkeleyDB)
find_package(BerkeleyDB 4.8...5.99 REQUIRED)

add_library(tapyrus_wallet STATIC
        coincontrol.cpp
        coinselection.cpp
        crypter.cpp
        db.cpp
        feebumper.cpp
        fees.cpp
        init.cpp
        ../interfaces/wallet.cpp
        pstt.cpp
        rpcdump.cpp
        rpcwallet.cpp
        wallet.cpp
        walletdb.cpp
        walletutil.cpp
)
target_link_libraries(tapyrus_wallet
  PRIVATE
    core_interface
    tapyrus_server
    univalue
    ${BerkeleyDB_LIBRARY}
    Boost::headers
    $<TARGET_NAME_IF_EXISTS:USDT::headers>
)
message(STATUS "BerkeleyDB_INCLUDE_DIR: ${BerkeleyDB_INCLUDE_DIR}")
target_include_directories(tapyrus_wallet PUBLIC ${BerkeleyDB_INCLUDE_DIR})