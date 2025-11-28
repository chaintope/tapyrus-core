# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

function(generate_setup_nsi)
  set(abs_top_srcdir ${PROJECT_SOURCE_DIR})
  set(abs_top_builddir ${PROJECT_BINARY_DIR})
  set(CLIENT_URL ${PROJECT_HOMEPAGE_URL})
  set(PACKAGE_TARNAME "tapyrus")
  set(TAPYRUS_GUI_NAME "tapyrus-qt")
  set(TAPYRUS_DAEMON_NAME "tapyrusd")
  set(TAPYRUS_CLI_NAME "tapyrus-cli")
  set(TAPYRUS_TX_NAME "tapyrus-tx")
  set(TAPYRUS_GENESIS_NAME "tapyrus-genesis")
  set(TAPYRUS_WALLET_TOOL_NAME "tapyrus-wallet")
  set(TAPYRUS_TEST_NAME "test_tapyrus")
  set(EXEEXT ${CMAKE_EXECUTABLE_SUFFIX})
  set(WINDOWS_BITS "64")
  set(PACKAGE_URL ${PROJECT_HOMEPAGE_URL})
  configure_file(${PROJECT_SOURCE_DIR}/share/setup.nsi.in ${PROJECT_BINARY_DIR}/tapyrus-win64-setup.nsi USE_SOURCE_PERMISSIONS @ONLY)
endfunction()
