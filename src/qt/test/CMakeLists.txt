# Copyright (c) 2018 The Bitcoin developers

project(test-tapyrus-qt)

include(TestSuite)
create_test_suite(tapyrus-qt)
add_dependencies(check check-tapyrus-qt)

add_test_to_suite(tapyrus-qt test-tapyrus-qt
	addressbooktests.cpp
	compattests.cpp
	rpcnestedtests.cpp
	test_main.cpp
	uritests.cpp
	util.cpp

	# Test framework
	../../test/test_tapyrus.cpp
)

# ../../test/test_tapyrus.cpp is a non-Qt shared fixture file.
# Prevent AutoMOC from generating a stale moc_ file for it (it has no
# Q_OBJECT and the header lives outside src/qt), and prevent AutoUIC from
# mistaking the "ui_interface.h" include for a Qt Designer .ui file.
set_source_files_properties(../../test/test_tapyrus.cpp PROPERTIES
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

target_link_libraries(test-tapyrus-qt tapyrusqt Qt6::Test)
import_plugins(test-tapyrus-qt)

# Wallet
if(ENABLE_WALLET)
	# Add wallet functionality to test_tapyrus_qt
	target_sources(test-tapyrus-qt
		PRIVATE
			wallettests.cpp
			../../wallet/test/wallet_test_fixture.cpp
	)

	target_link_libraries(test-tapyrus-qt tapyrus_wallet)
endif()
