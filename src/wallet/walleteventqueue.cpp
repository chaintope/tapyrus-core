// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/walleteventqueue.h>

#include <chain.h>
#include <cs_main.h>
#include <primitives/block.h>
#include <reverselock.h>
#include <sync.h>
#include <util.h>

#include <algorithm>
#include <cassert>

WalletEventQueue::~WalletEventQueue()
{
    Stop();
}

void WalletEventQueue::Start()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        assert(!m_running && !m_thread.joinable());
        m_stop = false;
        m_running = true;
        m_subscribed = true;
    }
    m_thread = std::thread(&TraceThread, "wallet", [this] { ThreadMain(); });
    RegisterValidationInterface(this);
}

void WalletEventQueue::Stop()
{
    bool subscribed;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        subscribed = m_subscribed;
        m_subscribed = false;
        m_stop = true;
    }
    if (subscribed) UnregisterValidationInterface(this);
    m_cond.notify_all();
    if (m_thread.joinable()) m_thread.join();

    std::lock_guard<std::mutex> lock(m_mutex);
    for (CWallet* wallet : m_wallets) wallet->SetEventQueue(nullptr);
    m_wallets.clear();
    m_tasks.clear();
}

void WalletEventQueue::Register(CWallet& wallet)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (std::find(m_wallets.begin(), m_wallets.end(), &wallet) != m_wallets.end()) return;
        m_wallets.push_back(&wallet);
    }
    wallet.SetEventQueue(this);
}

void WalletEventQueue::Unregister(CWallet& wallet)
{
    // A delivery takes cs_main and cs_wallet, so waiting for one with them held could deadlock.
    AssertLockNotHeld(cs_main);
    AssertLockNotHeld(wallet.cs_wallet);
    wallet.SetEventQueue(nullptr);
    std::unique_lock<std::mutex> lock(m_mutex);
    m_wallets.erase(std::remove(m_wallets.begin(), m_wallets.end(), &wallet), m_wallets.end());
    if (OnWalletThread()) return;
    // Counts, not the flags: the wallet thread starts the next event without releasing m_mutex.
    const uint64_t delivered{m_delivered + (m_delivering ? 1 : 0)};
    const uint64_t tasks_done{m_tasks_done + (m_task_running ? 1 : 0)};
    m_cond.wait(lock, [this, delivered, tasks_done] {
        return (m_delivered >= delivered && m_tasks_done >= tasks_done) || !m_running;
    });
}

void WalletEventQueue::ResendWalletTransactions()
{
    std::vector<CWallet*> wallets;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        wallets = m_wallets;
    }
    for (CWallet* wallet : wallets) wallet->ResendWalletTransactions();
}

void WalletEventQueue::ScheduleEvery(std::function<void()> task, std::chrono::milliseconds interval)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tasks.push_back({std::move(task), interval, std::chrono::steady_clock::now() + interval});
    }
    m_cond.notify_all();
}

void WalletEventQueue::CancelTasks()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_tasks.clear();
    if (OnWalletThread()) return;
    m_cond.wait(lock, [this] { return !m_task_running; });
}

void WalletEventQueue::Sync()
{
    AssertLockNotHeld(cs_main);
    std::unique_lock<std::mutex> lock(m_mutex);
    if (OnWalletThread()) return;
    const uint64_t target{m_queued};
    m_cond.wait(lock, [this, target] { return m_delivered >= target || !m_running; });
}

void WalletEventQueue::Add(Event event, bool block)
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        // The wallet thread cannot wait for itself to drain the queue.
        m_cond.wait(lock, [this, block] {
            const bool room{block ? m_pending_blocks < MAX_PENDING_BLOCK_EVENTS : m_events.size() - m_pending_blocks < MAX_PENDING_OTHER_EVENTS};
            return room || m_stop || !m_running || OnWalletThread();
        });
        if (!m_running) return;
        m_events.push_back({std::move(event), block});
        if (block) ++m_pending_blocks;
        ++m_queued;
    }
    m_cond.notify_all();
}

void WalletEventQueue::ThreadMain()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_thread_id = std::this_thread::get_id();
    while (true) {
        if (!m_events.empty()) {
            Event event{std::move(m_events.front().event)};
            if (m_events.front().block) --m_pending_blocks;
            m_events.pop_front();
            const std::vector<CWallet*> wallets{m_wallets};
            m_delivering = true;
            m_cond.notify_all();
            {
                reverse_lock<std::unique_lock<std::mutex>> unlock(lock);
                for (CWallet* wallet : wallets) event(*wallet);
            }
            m_delivering = false;
            ++m_delivered;
            m_cond.notify_all();
            continue;
        }
        // Stops once the events queued before Stop are delivered.
        if (m_stop) break;

        const auto now{std::chrono::steady_clock::now()};
        auto next{std::chrono::steady_clock::time_point::max()};
        bool ran_task{false};
        for (PeriodicTask& periodic : m_tasks) {
            if (periodic.next <= now) {
                periodic.next = now + periodic.interval;
                std::function<void()> task{periodic.task};
                m_task_running = true;
                {
                    reverse_lock<std::unique_lock<std::mutex>> unlock(lock);
                    task();
                }
                m_task_running = false;
                ++m_tasks_done;
                m_cond.notify_all();
                // Queued events go first, and m_tasks may have changed meanwhile.
                ran_task = true;
                break;
            }
            next = std::min(next, periodic.next);
        }
        if (ran_task) continue;
        if (next == std::chrono::steady_clock::time_point::max()) {
            m_cond.wait(lock);
        } else {
            m_cond.wait_until(lock, next);
        }
    }
    m_running = false;
    m_thread_id = std::thread::id();
    m_cond.notify_all();
}

void WalletEventQueue::UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload)
{
    Add([pindexNew, pindexFork, fInitialDownload](CWallet& wallet) { wallet.UpdatedBlockTip(pindexNew, pindexFork, fInitialDownload); }, false);
}

void WalletEventQueue::TransactionAddedToMempool(const CTransactionRef& ptx)
{
    Add([ptx](CWallet& wallet) { wallet.TransactionAddedToMempool(ptx); }, false);
}

void WalletEventQueue::BlockConnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex, const std::vector<CTransactionRef>& txnConflicted)
{
    Add([block, pindex, txnConflicted](CWallet& wallet) { wallet.BlockConnected(block, pindex, txnConflicted); }, true);
}

void WalletEventQueue::BlockDisconnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex)
{
    Add([block, pindex](CWallet& wallet) { wallet.BlockDisconnected(block, pindex); }, true);
}

void WalletEventQueue::TransactionRemovedFromMempool(const CTransactionRef& ptx)
{
    Add([ptx](CWallet& wallet) { wallet.TransactionRemovedFromMempool(ptx); }, false);
}

void WalletEventQueue::ChainStateFlushed(const CBlockLocator& locator)
{
    Add([locator](CWallet& wallet) { wallet.ChainStateFlushed(locator); }, false);
}
