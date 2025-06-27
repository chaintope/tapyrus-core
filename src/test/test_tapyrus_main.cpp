// Copyright (c) 2011-2018 The Bitcoin Core developers
// Copyright (c) 2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define BOOST_TEST_MODULE Tapyrus Test Suite

#include <boost/test/included/unit_test.hpp>
#include <net.h>

#include <memory>

std::unique_ptr<CConnman> g_connman;

[[noreturn]] void Shutdown()
{
  std::exit(EXIT_SUCCESS);
}

[[noreturn]] void StartShutdown()
{
  std::exit(EXIT_SUCCESS);
}

bool ShutdownRequested()
{
  return false;
}
