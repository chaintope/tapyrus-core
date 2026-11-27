// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensusconstants.h>
#include <key.h>
#include <mempoolremovalreason.h>
#include <primitives/transaction.h>
#include <script/standard.h>
#include <txmempool.h>
#include <validationinterface.h>
#include <wallet/test/wallet_test_fixture.h>
#include <wallet/walleteventqueue.h>

#include <atomic>
#include <future>
#include <thread>

#include <boost/signals2/connection.hpp>
#include <boost/test/unit_test.hpp>

/** Sends the node's mempool events for transactions paying the fixture's wallet. */
class WalletEventQueueSetup : public WalletTestingSetup
{
public:
    CKey m_key;

    WalletEventQueueSetup()
    {
        m_key.MakeNewKey(true);
        LOCK(m_wallet.cs_wallet);
        m_wallet.AddKeyPubKey(m_key, m_key.GetPubKey());
    }

    //! A transaction paying the wallet's key; seed makes each one distinct.
    CTransactionRef Payment(uint32_t seed)
    {
        CMutableTransaction mtx;
        mtx.vin.emplace_back(COutPoint(uint256S("01"), seed));
        mtx.vout.emplace_back(ConsensusConstants::COIN, GetScriptForDestination(m_key.GetPubKey().GetID()));
        return MakeTransactionRef(std::move(mtx));
    }

    //! Waits until the node's queue has handed its events on and the wallet thread has delivered them.
    void SyncBoth()
    {
        SyncWithValidationInterfaceQueue();
        m_wallet_events.Sync();
    }

    //! Sends the removal the way the mempool does.
    void RemoveFromMempool(const CTransactionRef& tx, MemPoolRemovalReason reason) { m_node.mempool->NotifyEntryRemoved(tx, reason); }

    bool InWallet(const CTransactionRef& tx) { return m_wallet.GetWalletTx(tx->GetHashMalFix()) != nullptr; }

    bool InMempool(const CTransactionRef& tx)
    {
        LOCK(m_wallet.cs_wallet);
        const CWalletTx* wtx = m_wallet.GetWalletTx(tx->GetHashMalFix());
        BOOST_REQUIRE(wtx);
        return wtx->fInMempool;
    }
};

BOOST_FIXTURE_TEST_SUITE(walleteventqueue_tests, WalletEventQueueSetup)

BOOST_AUTO_TEST_CASE(a_node_event_reaches_the_wallet_through_the_queue)
{
    const CTransactionRef tx{Payment(0)};
    GetMainSignals().TransactionAddedToMempool(tx);
    SyncBoth();
    BOOST_CHECK(InWallet(tx));
    BOOST_CHECK(InMempool(tx));
}

BOOST_AUTO_TEST_CASE(events_are_delivered_in_the_order_the_node_sent_them)
{
    const CTransactionRef removed_last{Payment(0)};
    GetMainSignals().TransactionAddedToMempool(removed_last);
    RemoveFromMempool(removed_last, MemPoolRemovalReason::EXPIRY);

    const CTransactionRef added_last{Payment(1)};
    GetMainSignals().TransactionAddedToMempool(added_last);
    RemoveFromMempool(added_last, MemPoolRemovalReason::EXPIRY);
    GetMainSignals().TransactionAddedToMempool(added_last);

    SyncBoth();
    BOOST_CHECK(!InMempool(removed_last));
    BOOST_CHECK(InMempool(added_last));
}

BOOST_AUTO_TEST_CASE(a_periodic_task_runs_on_the_wallet_thread_and_may_sync)
{
    std::promise<std::thread::id> ran;
    std::atomic<bool> once{false};
    m_wallet_events.ScheduleEvery([&] {
        if (once.exchange(true)) return;
        // Would deadlock if Sync waited for its own thread.
        m_wallet_events.Sync();
        ran.set_value(std::this_thread::get_id());
    }, std::chrono::milliseconds{1});
    std::future<std::thread::id> future = ran.get_future();
    BOOST_REQUIRE(future.wait_for(std::chrono::seconds{10}) == std::future_status::ready);
    // The task captures locals by reference.
    m_wallet_events.CancelTasks();
    BOOST_CHECK(future.get() != std::this_thread::get_id());
}

BOOST_AUTO_TEST_CASE(stop_delivers_the_events_already_queued)
{
    const CTransactionRef tx{Payment(0)};
    GetMainSignals().TransactionAddedToMempool(tx);
    // Hands the event to the wallet queue without waiting for its delivery.
    SyncWithValidationInterfaceQueue();
    m_wallet_events.Stop();
    BOOST_CHECK(InWallet(tx));
}

BOOST_AUTO_TEST_CASE(unregister_waits_for_the_delivery_in_progress)
{
    std::promise<void> entered;
    std::promise<void> release;
    const std::shared_future<void> released{release.get_future()};
    std::atomic<bool> once{false};
    std::atomic<bool> finished{false};
    // Blocks the delivery inside the wallet until the test releases it.
    const boost::signals2::scoped_connection blocker{m_wallet.NotifyTransactionChanged.connect(
        [&](CWallet*, const uint256&, ChangeType) {
            if (once.exchange(true)) return;
            entered.set_value();
            released.wait();
            finished = true;
        })};
    GetMainSignals().TransactionAddedToMempool(Payment(0));
    const bool blocked{entered.get_future().wait_for(std::chrono::seconds{10}) == std::future_status::ready};
    if (!blocked) release.set_value();
    BOOST_REQUIRE(blocked);

    std::future<bool> unregistered = std::async(std::launch::async, [&] {
        m_wallet_events.Unregister(m_wallet);
        return finished.load();
    });
    BOOST_CHECK(unregistered.wait_for(std::chrono::milliseconds{100}) == std::future_status::timeout);
    release.set_value();
    BOOST_CHECK(unregistered.get());
}

// Negative cases: wallets the queue does not deliver to.

BOOST_AUTO_TEST_CASE(an_unregistered_wallet_gets_no_events)
{
    m_wallet_events.Unregister(m_wallet);
    const CTransactionRef tx{Payment(0)};
    GetMainSignals().TransactionAddedToMempool(tx);
    SyncBoth();
    BOOST_CHECK(!InWallet(tx));
}

BOOST_AUTO_TEST_CASE(a_stopped_queue_delivers_nothing_and_sync_returns)
{
    m_wallet_events.Stop();
    const CTransactionRef tx{Payment(0)};
    GetMainSignals().TransactionAddedToMempool(tx);
    SyncBoth();
    BOOST_CHECK(!InWallet(tx));
}

BOOST_AUTO_TEST_CASE(a_wallet_registered_twice_is_gone_after_one_unregister)
{
    m_wallet_events.Register(m_wallet);
    m_wallet_events.Unregister(m_wallet);
    const CTransactionRef tx{Payment(0)};
    GetMainSignals().TransactionAddedToMempool(tx);
    SyncBoth();
    BOOST_CHECK(!InWallet(tx));
}

BOOST_AUTO_TEST_CASE(unregister_on_the_wallet_thread_returns_without_waiting)
{
    std::promise<void> unregistered;
    std::atomic<bool> once{false};
    m_wallet_events.ScheduleEvery([&] {
        if (once.exchange(true)) return;
        // Would deadlock if Unregister waited for the task it runs in.
        m_wallet_events.Unregister(m_wallet);
        unregistered.set_value();
    }, std::chrono::milliseconds{1});
    std::future<void> future = unregistered.get_future();
    BOOST_REQUIRE(future.wait_for(std::chrono::seconds{10}) == std::future_status::ready);
    // The task captures locals by reference.
    m_wallet_events.CancelTasks();

    const CTransactionRef tx{Payment(0)};
    GetMainSignals().TransactionAddedToMempool(tx);
    SyncBoth();
    BOOST_CHECK(!InWallet(tx));
}

BOOST_AUTO_TEST_SUITE_END()
