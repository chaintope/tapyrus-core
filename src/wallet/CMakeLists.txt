# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Add Berkeley DB dependency. Version range matches the top-level
# find_package(BerkeleyDB ...) call in CMakeLists.txt -- see the comment
# there for why 5.99 is a license ceiling, not a technical one, and how
# WITH_INCOMPATIBLE_BDB lifts it.
include(FindBerkeleyDB)
if(WITH_INCOMPATIBLE_BDB)
  find_package(BerkeleyDB 4.8 REQUIRED)
else()
  find_package(BerkeleyDB 4.8...5.99 REQUIRED)
endif()

# rpcwallet.cpp, rpcdump.cpp, and walletinit_rpc.cpp are deliberately NOT
# listed here -- they compile into tapyrus_rpc instead (rpcdump.cpp is pure
# RPC-command-handler content that rpcwallet.cpp's own RPC table extern-
# declares and registers; walletinit_rpc.cpp holds the one WalletInit method
# that calls into RegisterWalletRPCCommands()), so that tapyrus_wallet never
# needs to link tapyrus_rpc back. See the dependency graph at the top of the
# top-level src/CMakeLists.txt.
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
        tokenissuance.cpp
        wallet.cpp
        walletdb.cpp
        walletutil.cpp
)
target_link_libraries(tapyrus_wallet
  PUBLIC
    tapyrus_chainstate
    tapyrus_global
    tapyrus_scripting
  PRIVATE
    core_interface
    tapyrus_codec
    tapyrus_peer
    tapyrus_util
    univalue
    ${BerkeleyDB_LIBRARY}
    Boost::headers
)
message(STATUS "BerkeleyDB_INCLUDE_DIR: ${BerkeleyDB_INCLUDE_DIR}")
target_include_directories(tapyrus_wallet PUBLIC ${BerkeleyDB_INCLUDE_DIR})
