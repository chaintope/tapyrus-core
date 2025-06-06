# Copyright (c) 2017 The Bitcoin developers

project(wallet)

# Add Berkeley DB dependency.
find_package(BerkeleyDB REQUIRED)

# Add event dependency. This is only required for evhttp_uridecode
# in rpcwallet.cpp so it may be worth considering using an alternative.
find_package(Event REQUIRED)

add_library(wallet
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

target_link_libraries(wallet util univalue Event ${BerkeleyDB_LIBRARY})

target_include_directories(wallet PUBLIC ${BerkeleyDB_INCLUDE_DIR})
