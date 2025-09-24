// Copyright (c) 2012-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHECKQUEUE_H
#define BITCOIN_CHECKQUEUE_H

#include <sync.h>

#include <algorithm>
#include <vector>


template <typename T>
class CCheckQueueControl;

/**
 * Queue for verifications that have to be performed.
  * The verifications are represented by a type T, which must provide an
  * operator(), returning a bool.
  *
  * One thread (the master) is assumed to push batches of verifications
  * onto the queue, where they are processed by N-1 worker threads. When
  * the master is done adding work, it temporarily joins the worker pool
  * as an N'th worker, until all jobs are done.
  */
template <typename T>
class CCheckQueue
{
private:
    //! Mutex to protect the inner state
    Mutex mutex;

    //! Worker threads block on this when out of work
    std::condition_variable condWorker;

    //! Master thread blocks on this when out of work
    std::condition_variable condMaster;

    //! The queue of elements to be processed.
    //! As the order of booleans doesn't matter, it is used as a LIFO (stack)
    std::vector<T> queue;

    //! The number of workers (including the master) that are idle.
    int nIdle;

    //! The total number of workers (including the master).
    int nTotal;

    //! The temporary evaluation result.
    bool fAllOk;

    /**
     * Number of verifications that haven't completed yet.
     * This includes elements that are no longer queued, but still in the
     * worker's own batches.
     */
    unsigned int nTodo;

    //! The maximum number of elements to be processed in one batch
    unsigned int nBatchSize;

    std::vector<std::thread> m_worker_threads;
    bool m_request_stop;

    /** Internal function that does bulk of the verification work. */
    bool Loop(bool fMaster = false)
    {
        std::condition_variable& cond = fMaster ? condMaster : condWorker;
        std::vector<T> vChecks;
        vChecks.reserve(nBatchSize);
        unsigned int nNow = 0;
        bool fOk = true;
        do {
            {
                WaitableLock lock(mutex);
                // first do the clean-up of the previous loop run (allowing us to do it in the same critsect)
                if (nNow) {
                    fAllOk &= fOk;
                    nTodo -= nNow;
                    if (nTodo == 0 && !fMaster)
                        // We processed the last element; inform the master it can exit and return the result
                        condMaster.notify_one();
                } else {
                    // first iteration
                    nTotal++;
                }
                // logically, the do loop starts here
                while (queue.empty() && !m_request_stop) {
                    if (fMaster && nTodo == 0) {
                        nTotal--;
                        bool fRet = fAllOk;
                        // reset the status for new work later
                        if (fMaster)
                            fAllOk = true;
                        // return the current status
                        return fRet;
                    }
                    nIdle++;
                    // Use condition variable with predicate to avoid spurious wakeups and race conditions
                    if (fMaster) {
                        cond.wait(lock, [this]{ return !queue.empty() || m_request_stop || nTodo == 0; });
                    } else {
                        cond.wait(lock, [this]{ return !queue.empty() || m_request_stop; });
                    }
                    nIdle--;
                }
                if (m_request_stop) {
                    return false;
                }
                // Decide how many work units to process now.
                // * Do not try to do everything at once, but aim for increasingly smaller batches so
                //   all workers finish approximately simultaneously.
                // * Try to account for idle jobs which will instantly start helping.
                // * Don't do batches smaller than 1 (duh), or larger than nBatchSize.
                nNow = std::min(nBatchSize, (unsigned int)queue.size() / (nTotal + nIdle + 1));
                if (nNow == 0 && !queue.empty()) {
                    nNow = 1;  // Take at least 1 item if queue is not empty
                }
                if (nNow > 0) {
                    auto start_it = queue.end() - nNow;
                    vChecks.assign(std::make_move_iterator(start_it), std::make_move_iterator(queue.end()));
                    queue.erase(start_it, queue.end());
                }
                // Check whether we need to do work at all
                fOk = fAllOk;
            }
            // execute work
            for (T& check : vChecks)
                if (fOk)
                    fOk = check();
            vChecks.clear();
        } while (true);
    }

    /** Worker thread function - calls Loop in worker mode. */
    void WorkerLoop()
    {
        Loop(false);
    }

public:
    //! Mutex to ensure only one concurrent CCheckQueueControl
    std::mutex ControlMutex;

    //! Create a new check queue
    explicit CCheckQueue(unsigned int batch_size, int worker_threads_num) : nIdle(0), nTotal(0), fAllOk(true), nTodo(0), nBatchSize(batch_size), m_request_stop(false)
    {
        {
            WaitableLock loc(mutex);
            nIdle = 0;
            nTotal = 0;
            fAllOk = true;
        }
        assert(m_worker_threads.empty());
        for (int n = 0; n < worker_threads_num; ++n) {
            m_worker_threads.emplace_back([this, n]() {
                RenameThread(strprintf("scriptch.%i", n));
                WorkerLoop();
        });
        }
    }

    // Since this class manages its own resources, which is a thread
    // pool `m_worker_threads`, copy and move operations are not appropriate.
    CCheckQueue(const CCheckQueue&) = delete;
    CCheckQueue& operator=(const CCheckQueue&) = delete;
    CCheckQueue(CCheckQueue&&) = delete;
    CCheckQueue& operator=(CCheckQueue&&) = delete;

    //! Wait until execution finishes, and return whether all evaluations were successful.
    bool Wait()
    {
        return Loop(true);
    }

    //! Add a batch of checks to the queue
    void Add(std::vector<T>&& vChecks)
    {
        if (vChecks.empty()) {
            return;
        }

        {
            WaitableLock lock(mutex);
            queue.insert(queue.end(), std::make_move_iterator(vChecks.begin()), std::make_move_iterator(vChecks.end()));
            nTodo += vChecks.size();
        }

        if (vChecks.size() == 1)
            condWorker.notify_one();
        else if (vChecks.size() > 1)
            condWorker.notify_all();
    }

    //! Stop all of the worker threads.
    ~CCheckQueue()
    {
        {
            WaitableLock lock(mutex);
            m_request_stop = true;
        }
        condWorker.notify_all();
        for (std::thread& t : m_worker_threads) {
            t.join();
        }
    }

};

/**
 * RAII-style controller object for a CCheckQueue that guarantees the passed
 * queue is finished before continuing.
 */
template <typename T>
class CCheckQueueControl
{
private:
    CCheckQueue<T> * const pqueue;
    bool fDone;

public:
    CCheckQueueControl() = delete;
    CCheckQueueControl(const CCheckQueueControl&) = delete;
    CCheckQueueControl& operator=(const CCheckQueueControl&) = delete;
    explicit CCheckQueueControl(CCheckQueue<T> * const pqueueIn) : pqueue(pqueueIn), fDone(false)
    {
        // passed queue is supposed to be unused, or nullptr
        if (pqueue != nullptr) {
            ENTER_CRITICAL_SECTION(pqueue->ControlMutex);
        }
    }

    bool Wait()
    {
        if (pqueue == nullptr)
            return true;
        bool fRet = pqueue->Wait();
        fDone = true;
        return fRet;
    }

    void Add(std::vector<T>&& vChecks)
    {
        if (pqueue != nullptr)
            pqueue->Add(std::move(vChecks));
    }

    ~CCheckQueueControl()
    {
        if (!fDone)
            Wait();
        if (pqueue != nullptr) {
            LEAVE_CRITICAL_SECTION(pqueue->ControlMutex);
        }
    }
};

#endif // BITCOIN_CHECKQUEUE_H
