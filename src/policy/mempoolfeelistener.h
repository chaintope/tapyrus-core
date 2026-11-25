// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_POLICY_MEMPOOLFEELISTENER_H
#define TAPYRUS_POLICY_MEMPOOLFEELISTENER_H

#include <boost/signals2/connection.hpp>

class CBlockPolicyEstimator;
class CTxMemPool;

/** Feeds the fee estimator the mempool's changes for as long as it exists, so
 *  neither knows the other. The mempool raises them synchronously with its lock
 *  held, so the estimator must take no other lock while handling them. */
class MempoolFeeListener
{
public:
    MempoolFeeListener(CTxMemPool& mempool, CBlockPolicyEstimator& estimator);

private:
    boost::signals2::scoped_connection m_entry_added;
    boost::signals2::scoped_connection m_entry_erased;
    boost::signals2::scoped_connection m_block;
};

#endif // TAPYRUS_POLICY_MEMPOOLFEELISTENER_H
