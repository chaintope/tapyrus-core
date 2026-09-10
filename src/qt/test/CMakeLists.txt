# Copyright (c) 2018 The Bitcoin developers

project(test-tapyrus-qt)

include(TestSuite)
create_test_suite(tapyrus-qt)
add_dependencies(check check-tapyrus-qt)

add_test_to_suite(tapyrus-qt test-tapyrus-qt
	compattests.cpp
	rpcnestedtests.cpp
	test_main.cpp
	uritests.cpp
	util.cpp

	# Test framework
	../../test/test_tapyrus.cpp

	# Glue -- see the six-glue-files comment on tapyrusd's add_executable()
	# in the top-level src/CMakeLists.txt. Unlike test_tapyrus (which has
	# its own test_tapyrus_main.cpp stubbing g_connman/StartShutdown/
	# ShutdownRequested), this binary's test_main.cpp calls noui_connect()
	# directly and defines none of these itself, so it needs the real
	# files -- all six, including noui.cpp this time.
	../../file_io.cpp
	../../init.cpp
	../../noui.cpp
	../../shutdown.cpp
	../../timeoffsets.cpp
	../../ui_interface.cpp
)

# ../../test/test_tapyrus.cpp is a non-Qt shared fixture file.
# Prevent AutoMOC from generating a stale moc_ file for it (it has no
# Q_OBJECT and the header lives outside src/qt), and prevent AutoUIC from
# mistaking the "ui_interface.h" include for a Qt Designer .ui file.
# init.cpp/timeoffsets.cpp/ui_interface.cpp hit the same ui_interface.h/
# AutoUIC collision -- see the matching set_property() in src/qt/CMakeLists.txt.
set_source_files_properties(
	../../test/test_tapyrus.cpp
	../../init.cpp
	../../noui.cpp
	../../timeoffsets.cpp
	../../ui_interface.cpp
	PROPERTIES
	SKIP_AUTOMOC ON
	SKIP_AUTOUIC ON
)

# Fix MOC path prefix for test sources: the parent directory sets
# CMAKE_AUTOMOC_MOC_OPTIONS to "-p<src/qt>" which makes MOC generate
# include paths under src/qt instead of src/qt/test. Override it at
# the target level so the generated moc_*.cpp files include headers
# from this directory.
set_target_properties(test-tapyrus-qt PROPERTIES
	AUTOMOC_MOC_OPTIONS "-p${CMAKE_CURRENT_SOURCE_DIR}"
)

target_include_directories(test-tapyrus-qt
	PRIVATE
		${CMAKE_SOURCE_DIR}/src/secp256k1/include
)

target_link_libraries(test-tapyrus-qt
	tapyrusqt
	tapyrus_chainstate
	tapyrus_peer
	tapyrus_rpc
	$<TARGET_NAME_IF_EXISTS:tapyrus_zmq>
	Qt6::Test
)
import_plugins(test-tapyrus-qt)

# Wallet
if(ENABLE_WALLET)
	# Add wallet functionality to test_tapyrus_qt
	#
	# addressbooktests.cpp is here, not in the base add_test_to_suite() list
	# above, even though it was there pre-split (confirmed identical on
	# ct/master) -- it directly uses CWallet/WalletModel and would fail to
	# link with -DENABLE_WALLET=OFF, same as wallettests.cpp already did.
	# This was a real pre-existing gap, just never exercised until GUI +
	# no-wallet were tried together.
	target_sources(test-tapyrus-qt
		PRIVATE
			addressbooktests.cpp
			wallettests.cpp
			../../wallet/test/wallet_test_fixture.cpp
	)

	target_link_libraries(test-tapyrus-qt tapyrus_wallet)
endif()
