// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainstatemanager.h>
#include <consensusconstants.h>
#include <cs_main.h>
#include <key.h>
#include <mempoolremovalreason.h>
#include <primitives/transaction.h>
#include <script/standard.h>
#include <txmempool.h>
#include <validationinterface.h>
#include <wallet/test/wallet_test_fixture.h>
#include <wallet/walleteventqueue.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

#include <boost/signals2/connection.hpp>
#include <boost/test/unit_test.hpp>

/** Holds the wallet thread inside the wallet's first transaction delivery until released. */
class DeliveryBlocker
{
public:
    explicit DeliveryBlocker(CWallet& wallet)
        : m_released{m_release.get_future()},
          m_connection{wallet.NotifyTransactionChanged.connect([this](CWallet*, const uint256&, ChangeType) {
              if (m_once.exchange(true)) return;
              m_entered.set_value();
              m_released.wait();
          })}
    {
    }
    ~DeliveryBlocker() { Release(); }

    //! True once the wallet thread is held, within ten seconds.
    bool WaitEntered() { return m_entered.get_future().wait_for(std::chrono::seconds{10}) == std::future_status::ready; }
    void Release()
    {
        if (!m_done.exchange(true)) m_release.set_value();
    }

private:
    std::promise<void> m_entered;
    std::promise<void> m_release;
    std::shared_future<void> m_released;
    std::atomic<bool> m_once{false};
    std::atomic<bool> m_done{false};
    boost::signals2::scoped_connection m_connection;
};

/** Sends the node's mempool events for transactions paying the fixture's wallet. */
class WalletEventQueueSetup : public WalletTestingSetup
{
public:
    CKey m_key;
    const CBlockIndex* m_tip;

    WalletEventQueueSetup()
    {
        m_key.MakeNewKey(true);
        LOCK2(cs_main, m_wallet.cs_wallet);
        m_tip = m_node.chainman->ActiveChain().Tip();
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

    //! Sends count payments to the node's queue, seeded from first.
    std::vector<CTransactionRef> SendPayments(uint32_t first, size_t count)
    {
        std::vector<CTransactionRef> txs;
        for (size_t i = 0; i < count; ++i) {
            txs.push_back(Payment(first + static_cast<uint32_t>(i)));
            GetMainSignals().TransactionAddedToMempool(txs.back());
        }
        return txs;
    }

    //! Sends count empty blocks connected at the tip, as block events for the queue's bound.
    //! Takes no lock: a held delivery holds cs_main.
    void SendBlocks(size_t count)
    {
        for (size_t i = 0; i < count; ++i) {
            GetMainSignals().BlockConnected(std::make_shared<const CBlock>(), m_tip, std::make_shared<const std::vector<CTransactionRef>>());
        }
    }

    //! True once the node's queue has exactly pending callbacks left, within ten seconds.
    bool WaitForCallbacksPending(size_t pending)
    {
        const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{10}};
        while (GetMainSignals().CallbacksPending() != pending) {
            if (std::chrono::steady_clock::now() > deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        return true;
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

// The bounds: with one delivery held, MAX_PENDING_BLOCK_EVENTS block events fill the
// queue, the next one waits in Add on the scheduler thread, and the one after
// that stays in the node's queue. Other events have their own, larger bound.
static constexpr size_t FILL_BLOCKS{WalletEventQueue::MAX_PENDING_BLOCK_EVENTS + 2};

BOOST_AUTO_TEST_CASE(add_waits_while_the_block_events_are_full_and_resumes_as_they_drain)
{
    DeliveryBlocker blocker{m_wallet};
    const CTransactionRef held{SendPayments(0, 1).front()};
    BOOST_REQUIRE(blocker.WaitEntered());

    SendBlocks(FILL_BLOCKS);
    BOOST_CHECK(WaitForCallbacksPending(1));
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    BOOST_CHECK_EQUAL(GetMainSignals().CallbacksPending(), 1U);

    blocker.Release();
    SyncBoth();
    BOOST_CHECK_EQUAL(GetMainSignals().CallbacksPending(), 0U);
    BOOST_CHECK(InWallet(held));
}

BOOST_AUTO_TEST_CASE(add_waits_once_the_other_events_are_full)
{
    DeliveryBlocker blocker{m_wallet};
    const CTransactionRef held{SendPayments(0, 1).front()};
    BOOST_REQUIRE(blocker.WaitEntered());

    const std::vector<CTransactionRef> txs{SendPayments(1, WalletEventQueue::MAX_PENDING_OTHER_EVENTS + 2)};
    BOOST_CHECK(WaitForCallbacksPending(1));

    blocker.Release();
    SyncBoth();
    BOOST_CHECK_EQUAL(GetMainSignals().CallbacksPending(), 0U);
    for (const CTransactionRef& tx : txs) BOOST_CHECK(InWallet(tx));
}

BOOST_AUTO_TEST_CASE(stop_wakes_a_waiting_add)
{
    DeliveryBlocker blocker{m_wallet};
    const CTransactionRef held{SendPayments(0, 1).front()};
    BOOST_REQUIRE(blocker.WaitEntered());
    SendBlocks(FILL_BLOCKS);
    BOOST_REQUIRE(WaitForCallbacksPending(1));

    std::future<void> stopped = std::async(std::launch::async, [&] { m_wallet_events.Stop(); });
    // The scheduler thread is free again although the delivery is still held.
    std::future<void> node_synced = std::async(std::launch::async, [] { SyncWithValidationInterfaceQueue(); });
    const bool synced{node_synced.wait_for(std::chrono::seconds{10}) == std::future_status::ready};
    blocker.Release();
    BOOST_CHECK(synced);
    stopped.get();
    BOOST_CHECK(InWallet(held));
}

BOOST_AUTO_TEST_CASE(unregister_completes_while_add_waits)
{
    DeliveryBlocker blocker{m_wallet};
    const CTransactionRef held{SendPayments(0, 1).front()};
    BOOST_REQUIRE(blocker.WaitEntered());
    SendBlocks(FILL_BLOCKS);
    const std::vector<CTransactionRef> txs{SendPayments(1, 2)};
    BOOST_REQUIRE(WaitForCallbacksPending(1 + txs.size()));

    // As unloadwallet does: waits only for the delivery in progress, not for the full queue.
    std::future<void> unregistered = std::async(std::launch::async, [&] { m_wallet_events.Unregister(m_wallet); });
    BOOST_CHECK(unregistered.wait_for(std::chrono::milliseconds{100}) == std::future_status::timeout);
    blocker.Release();
    BOOST_CHECK(unregistered.wait_for(std::chrono::seconds{10}) == std::future_status::ready);

    SyncBoth();
    BOOST_CHECK(InWallet(held));
    for (const CTransactionRef& tx : txs) BOOST_CHECK(!InWallet(tx));
}

BOOST_AUTO_TEST_CASE(a_rescan_holding_cs_main_finishes_while_add_waits)
{
    CTransactionRef held;
    {
        // As a wallet load rescans: the delivery waits for cs_main, so the queue fills.
        LOCK(cs_main);
        held = SendPayments(0, 1).front();
        SendBlocks(FILL_BLOCKS);
        BOOST_REQUIRE(WaitForCallbacksPending(1));

        WalletRescanReserver reserver(&m_wallet);
        BOOST_REQUIRE(reserver.reserve());
        const CBlockIndex* genesis{m_node.chainman->ActiveChain().Genesis()};
        const CWallet::ScanResult result{m_wallet.ScanForWalletTransactions(genesis->GetBlockHash(), genesis->nHeight, {} /* max_height */, reserver, true)};
        BOOST_CHECK(result.status == CWallet::ScanResult::SUCCESS);
    }
    SyncBoth();
    BOOST_CHECK(InWallet(held));
}

// Negative cases: no wait below the bounds, and wallets the queue does not deliver to.

BOOST_AUTO_TEST_CASE(add_does_not_wait_below_the_block_bound)
{
    DeliveryBlocker blocker{m_wallet};
    const CTransactionRef held{SendPayments(0, 1).front()};
    BOOST_REQUIRE(blocker.WaitEntered());

    SendBlocks(WalletEventQueue::MAX_PENDING_BLOCK_EVENTS);
    // Every event leaves the node's queue although none is delivered yet.
    BOOST_CHECK(WaitForCallbacksPending(0));

    blocker.Release();
    SyncBoth();
    BOOST_CHECK(InWallet(held));
}

BOOST_AUTO_TEST_CASE(transactions_beyond_the_block_bound_do_not_wait)
{
    DeliveryBlocker blocker{m_wallet};
    const CTransactionRef held{SendPayments(0, 1).front()};
    BOOST_REQUIRE(blocker.WaitEntered());

    // The wallet may lag behind the mempool while a block is validated.
    const std::vector<CTransactionRef> txs{SendPayments(1, 10 * WalletEventQueue::MAX_PENDING_BLOCK_EVENTS)};
    BOOST_CHECK(WaitForCallbacksPending(0));

    blocker.Release();
    SyncBoth();
    for (const CTransactionRef& tx : txs) BOOST_CHECK(InWallet(tx));
}

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
