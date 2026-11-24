// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_WALLET_WALLETEVENTQUEUE_H
#define TAPYRUS_WALLET_WALLETEVENTQUEUE_H

#include <validationinterface.h>
#include <wallet/wallet.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

/**
 * The wallets' own queue of validation events, run by the wallet thread.
 *
 * It subscribes to the node's validation interface once, and each callback
 * only queues the event, so the node never waits on wallet work. The queue
 * is unbounded: a wallet that falls behind holds the pending blocks. The
 * wallet thread delivers the events to the registered wallets in the order
 * the node sent them, and runs the wallets' periodic tasks between events.
 */
class WalletEventQueue final : public CValidationInterface, public WalletEvents
{
public:
    WalletEventQueue() = default;
    ~WalletEventQueue();

    //! Starts the wallet thread and subscribes to the node's validation interface.
    void Start();
    //! Unsubscribes, delivers the events already queued and joins the wallet thread.
    void Stop();

    void Register(CWallet& wallet) override;
    //! Waits for the delivery or task in progress to finish, so the wallet is not used once it returns.
    void Unregister(CWallet& wallet) override;

    //! Rebroadcasts the registered wallets' unconfirmed transactions; scheduled on the wallet thread.
    void ResendWalletTransactions();

    //! Runs task on the wallet thread every interval, first one interval after the call.
    void ScheduleEvery(std::function<void()> task, std::chrono::milliseconds interval);
    //! Drops the periodic tasks, waiting for one in progress to finish; events are still delivered.
    void CancelTasks();

    //! Returns at once on the wallet thread, which cannot wait for itself.
    void Sync() override;

protected:
    void UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload) override;
    void TransactionAddedToMempool(const CTransactionRef& ptx) override;
    void BlockConnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex, const std::vector<CTransactionRef>& txnConflicted) override;
    void BlockDisconnected(const std::shared_ptr<const CBlock>& block) override;
    void TransactionRemovedFromMempool(const CTransactionRef& ptx) override;
    void ChainStateFlushed(const CBlockLocator& locator) override;

private:
    using Event = std::function<void(CWallet&)>;

    struct PeriodicTask {
        std::function<void()> task;
        std::chrono::milliseconds interval;
        std::chrono::steady_clock::time_point next;
    };

    //! Queues event for every wallet registered when it is delivered.
    void Add(Event event);
    void ThreadMain();
    //! Requires m_mutex.
    bool OnWalletThread() const { return m_thread_id == std::this_thread::get_id(); }

    //! Guards every member below except m_thread.
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::deque<Event> m_events;
    std::vector<CWallet*> m_wallets;
    std::vector<PeriodicTask> m_tasks;
    //! Number of events ever queued, and of those fully delivered.
    uint64_t m_queued{0};
    uint64_t m_delivered{0};
    //! Number of periodic task runs finished.
    uint64_t m_tasks_done{0};
    bool m_delivering{false};
    bool m_task_running{false};
    bool m_subscribed{false};
    bool m_stop{false};
    bool m_running{false};
    std::thread::id m_thread_id;
    std::thread m_thread;
};

#endif // TAPYRUS_WALLET_WALLETEVENTQUEUE_H
