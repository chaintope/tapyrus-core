// Copyright (c) 2021-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/validation.h>
#include <key_io.h>
#include <policy/packages.h>
#include <policy/policy.h>
#include <policy/rbf.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <serialize.h>
#include <streams.h>
#include <random.h>
#include <script/script.h>
#include <test/test_tapyrus.h>
#include <utilstrencodings.h>
#include <util.h>
#include <validation.h>
#include <boost/test/unit_test.hpp>

struct PackageTestSetup: public TestChainSetup
{
    PackageTestSetup()
    {
        std::vector<unsigned char> coinbasePubKeyHash(20);
        CPubKey coinbasePubKey = coinbaseKey.GetPubKey();
        CHash160().Write(coinbasePubKey.data(), coinbasePubKey.size()).Finalize(
                coinbasePubKeyHash.data());
    }

    ~PackageTestSetup()
    {
    }

    void Sign(std::vector<unsigned char>& vchSig, CKey& signKey, const CScript& scriptPubKey, int inIndex, CMutableTransaction& outTx, int outIndex)
    {
        uint256 hash = SignatureHash(scriptPubKey, outTx, inIndex, SIGHASH_ALL, outTx.vout[outIndex].nValue, SigVersion::BASE);
        signKey.Sign_Schnorr(hash, vchSig);
        vchSig.push_back((unsigned char)SIGHASH_ALL);
    }

};

BOOST_AUTO_TEST_SUITE(txpackage_tests)

// Create placeholder transactions that have no meaning.
CMutableTransaction create_placeholder_tx(size_t num_inputs, size_t num_outputs) {
    CMutableTransaction mtx = CMutableTransaction();
    mtx.vin.resize(num_inputs);
    mtx.vout.resize(num_outputs);
    auto random_script = CScript() << ToByteVector(InsecureRand256()) << ToByteVector(InsecureRand256());
    for (size_t i{0}; i < num_inputs; ++i) {
        mtx.vin[i].prevout.hashMalFix = InsecureRand256();
        mtx.vin[i].prevout.n = 0;
        mtx.vin[i].scriptSig = random_script;
    }
    for (size_t o{0}; o < num_outputs; ++o) {
        mtx.vout[o].nValue = 1 * CENT;
        mtx.vout[o].scriptPubKey = random_script;
    }
    return mtx;
}

CMutableTransaction CreateValidTransaction(COutPoint& prevout, CAmount amt, CScript& script) {

    CMutableTransaction mtx = create_placeholder_tx(1, 1);
    mtx.vin[0].prevout = prevout;
    mtx.vout[0].nValue = amt;
    mtx.vout[0].scriptPubKey = script;
    return mtx;
}

CMutableTransaction CreateValidMempoolTransaction(CTxMemPool& mempool, COutPoint& prevout, CAmount amt, CScript& script) {
    //create a transaction with one input and one output from the given arguments.
    CMutableTransaction tx = CMutableTransaction();
    tx.vin.resize(1);
    tx.vout.resize(1);

    tx.vin[0].prevout = prevout;
    tx.vin[0].nSequence = MAX_BIP125_RBF_SEQUENCE;
    //no signature as the tx is not validated. It is added to mempool without checks.
    tx.vin[0].scriptSig = CScript();

    tx.vout[0].nValue = amt;
    tx.vout[0].scriptPubKey = script;

    TestMemPoolEntryHelper entry;
    {
        LOCK(cs_main);
        LOCK(mempool.cs);
        mempool.addUnchecked(tx.GetHashMalFix(), entry.Time(GetTime()).FromTx(tx));

    }
    return tx;

}

bool IsChildWithParents(const Package& package)
{
    assert(std::all_of(package.cbegin(), package.cend(), [](const auto& tx){return tx != nullptr;}));
    if (package.size() < 2) return false;

    // The package is expected to be sorted, so the last transaction is the child.
    const auto& child = package.back();
    std::unordered_set<uint256, SaltedTxidHasher> input_txids;
    std::transform(child->vin.cbegin(), child->vin.cend(),
                   std::inserter(input_txids, input_txids.end()),
                   [](const auto& input) { return input.prevout.hashMalFix; });

    // Every transaction must be a parent of the last transaction in the package.
    return std::all_of(package.cbegin(), package.cend() - 1,
                       [&input_txids](const auto& ptx) { return input_txids.count(ptx->GetHashMalFix()) > 0; });
}

bool IsChildWithParentsTree(const Package& package)
{
    if (!IsChildWithParents(package)) return false;
    std::unordered_set<uint256, SaltedTxidHasher> parent_txids;
    std::transform(package.cbegin(), package.cend() - 1, std::inserter(parent_txids, parent_txids.end()),
                   [](const auto& ptx) { return ptx->GetHashMalFix(); });
    // Each parent must not have an input who is one of the other parents.
    return std::all_of(package.cbegin(), package.cend() - 1, [&](const auto& ptx) {
        for (const auto& input : ptx->vin) {
            if (parent_txids.count(input.prevout.hashMalFix) > 0) return false;
        }
        return true;
    });
}


BOOST_FIXTURE_TEST_CASE(package_sanitization_tests, PackageTestSetup)
{
    // Packages can't have more than 25 transactions.
    Package package_too_many;
    package_too_many.reserve(MAX_PACKAGE_COUNT + 1);
    for (size_t i{0}; i < MAX_PACKAGE_COUNT + 1; ++i) {
        package_too_many.emplace_back(MakeTransactionRef(create_placeholder_tx(1, 1)));
    }
    CValidationState state_too_many;
    BOOST_CHECK(!CheckPackage(package_too_many, state_too_many));
    BOOST_CHECK_EQUAL(state_too_many.GetRejectCode(), REJECT_PACKAGE_INVALID);
    BOOST_CHECK_EQUAL(state_too_many.GetRejectReason(), "package-too-many-transactions");

    // Packages can't contain transactions with the same txid.
    Package package_duplicate_txids_empty;
    for (auto i{0}; i < 3; ++i) {
        CMutableTransaction empty_tx;
        package_duplicate_txids_empty.emplace_back(MakeTransactionRef(empty_tx));
    }
    CValidationState state_duplicates;
    BOOST_CHECK(!CheckPackage(package_duplicate_txids_empty, state_duplicates));
    BOOST_CHECK_EQUAL(state_duplicates.GetRejectCode(), REJECT_PACKAGE_INVALID);
    BOOST_CHECK_EQUAL(state_duplicates.GetRejectReason(), "package-contains-duplicates");

    // Packages can't have transactions spending the same prevout
    CMutableTransaction tx_zero_1;
    CMutableTransaction tx_zero_2;
    COutPoint same_prevout{InsecureRand256(), 0};
    tx_zero_1.vin.emplace_back(same_prevout);
    tx_zero_2.vin.emplace_back(same_prevout);
    // Different vouts (not the same tx)
    tx_zero_1.vout.emplace_back(CENT, CScript() << OP_TRUE);
    tx_zero_2.vout.emplace_back(2 * CENT, CScript() << OP_TRUE);
    Package package_conflicts{MakeTransactionRef(tx_zero_1), MakeTransactionRef(tx_zero_2)};

    CValidationState state_conflicts;
    BOOST_CHECK(!CheckPackage(package_conflicts, state_conflicts));
    BOOST_CHECK_EQUAL(state_conflicts.GetRejectCode(), REJECT_PACKAGE_INVALID);
    BOOST_CHECK_EQUAL(state_conflicts.GetRejectReason(), "conflict-in-package");

    // One transaction spending the same input twice is not identified at the package level.
    // It is identified later at transaction validation
    CMutableTransaction dup_tx;
    const COutPoint rand_prevout{InsecureRand256(), 0};
    dup_tx.vin.emplace_back(rand_prevout);
    dup_tx.vin.emplace_back(rand_prevout);
    Package package_with_dup_tx{MakeTransactionRef(dup_tx)};
    CValidationState state;
    BOOST_CHECK(CheckPackage(package_with_dup_tx, state));
    package_with_dup_tx.emplace_back(MakeTransactionRef(create_placeholder_tx(1, 1)));
    BOOST_CHECK(CheckPackage(package_with_dup_tx, state));
}

BOOST_FIXTURE_TEST_CASE(package_validation_tests, PackageTestSetup)
{
    PackageValidationState validationState;
    CValidationState state;
    CTxMempoolAcceptanceOptions opt;
    opt.context = ValidationContext::PACKAGE;

    CKey parent_key;
    parent_key.MakeNewKey(true);
    CScript parent_locking_script = GetScriptForDestination(parent_key.GetPubKey().GetID());

    CKey child_key;
    child_key.MakeNewKey(true);
    CScript child_locking_script = GetScriptForDestination(child_key.GetPubKey().GetID());
    unsigned int initialPoolSize = mempool.size();
    unsigned long index_cb = m_coinbase_txns.size();//init this index before refilling coinbase

    refillCoinbase(50);

    // Parent and Child Package
    // both in mempool
    {
        COutPoint spend_coinbase(m_coinbase_txns[index_cb]->GetHashMalFix(), 0);
        CMutableTransaction mtx_parent = CreateValidMempoolTransaction(mempool, spend_coinbase, CAmount(49 * COIN), parent_locking_script);

        COutPoint spend_parent(mtx_parent.GetHashMalFix(), 0);
        CMutableTransaction mtx_child = CreateValidMempoolTransaction(mempool, spend_parent, CAmount(48 * COIN), child_locking_script);

        Package package_parent_child{MakeTransactionRef(mtx_parent), MakeTransactionRef(mtx_child)};

        auto result_parent_child = SubmitPackageToMempool(package_parent_child, state, validationState, opt);
        
        BOOST_CHECK(!result_parent_child);
        BOOST_CHECK_EQUAL(state.GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "");
        BOOST_CHECK_EQUAL(validationState[mtx_parent.GetHashMalFix()].GetRejectCode(), REJECT_DUPLICATE);
        BOOST_CHECK_EQUAL(validationState[mtx_parent.GetHashMalFix()].GetRejectReason(), "txn-already-in-mempool");
        BOOST_CHECK_EQUAL(validationState[mtx_child.GetHashMalFix()].GetRejectCode(), REJECT_DUPLICATE);
        BOOST_CHECK_EQUAL(validationState[mtx_child.GetHashMalFix()].GetRejectReason(), "txn-already-in-mempool");

        BOOST_CHECK_EQUAL(mempool.size(), initialPoolSize + 2);
    }

    // Parent and Child Package
    // both not in mempool
    {
        ++index_cb;
        std::vector<unsigned char> vchSig;

        COutPoint spend_cb(m_coinbase_txns[index_cb]->GetHashMalFix(), 0);
        auto mtx_parent = CreateValidTransaction(spend_cb, CAmount(49 * COIN), {CScript() << OP_TRUE << OP_EQUAL});
        Sign(vchSig, coinbaseKey, m_coinbase_txns[index_cb]->vout[0].scriptPubKey, 0, mtx_parent, 0);
        mtx_parent.vin[0].scriptSig = CScript() << vchSig;

        COutPoint spend_parent(mtx_parent.GetHashMalFix(), 0);
        auto mtx_child = CreateValidTransaction(spend_parent, CAmount(44 * COIN), child_locking_script);
        mtx_child.vin[0].scriptSig = CScript() << OP_TRUE;
        for(auto x : {0,1,2,3})
            mtx_child.vout.push_back(CTxOut(CAmount(1 * COIN), {CScript() << OP_TRUE << OP_EQUAL}));

        Package package_parent_child{MakeTransactionRef(mtx_parent), MakeTransactionRef(mtx_child)};
        validationState.clear();

        auto result_parent_child = SubmitPackageToMempool(package_parent_child, state, validationState, opt);

        BOOST_CHECK(result_parent_child);
        BOOST_CHECK_EQUAL(state.GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "");
        BOOST_CHECK_EQUAL(validationState[mtx_parent.GetHashMalFix()].GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(validationState[mtx_parent.GetHashMalFix()].GetRejectReason(), "");
        BOOST_CHECK_EQUAL(validationState[mtx_child.GetHashMalFix()].GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(validationState[mtx_child.GetHashMalFix()].GetRejectReason(), "");

        // Check that mempool size hasn't changed.
        BOOST_CHECK_EQUAL(mempool.size(), initialPoolSize + 4);
    }

    // Parent in mempool
    {
        ++index_cb;
        std::vector<unsigned char> vchSig;

        COutPoint spend_cb(m_coinbase_txns[index_cb]->GetHashMalFix(), 0);
        auto mtx_parent = CreateValidMempoolTransaction(mempool, spend_cb, CAmount(49 * COIN), {CScript() << OP_TRUE << OP_EQUAL});
        Sign(vchSig, coinbaseKey, m_coinbase_txns[index_cb]->vout[0].scriptPubKey, 0, mtx_parent, 0);
        mtx_parent.vin[0].scriptSig = CScript() << vchSig;

        COutPoint spend_parent(mtx_parent.GetHashMalFix(), 0);
        auto mtx_child = CreateValidTransaction(spend_parent, CAmount(44 * COIN), child_locking_script);
        mtx_child.vin[0].scriptSig = CScript() << OP_TRUE;
        for(auto x : {0,1,2,3})
            mtx_child.vout.push_back(CTxOut(CAmount(1 * COIN), {CScript() << OP_TRUE << OP_EQUAL}));

        Package package_parent_child{MakeTransactionRef(mtx_parent), MakeTransactionRef(mtx_child)};
        validationState.clear();

        auto result_parent_child = SubmitPackageToMempool(package_parent_child, state, validationState, opt);

        BOOST_CHECK(!result_parent_child);
        BOOST_CHECK_EQUAL(state.GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "");
        BOOST_CHECK_EQUAL(validationState[mtx_parent.GetHashMalFix()].GetRejectCode(), REJECT_DUPLICATE);
        BOOST_CHECK_EQUAL(validationState[mtx_parent.GetHashMalFix()].GetRejectReason(), "txn-already-in-mempool");
        BOOST_CHECK_EQUAL(validationState[mtx_child.GetHashMalFix()].GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(validationState[mtx_child.GetHashMalFix()].GetRejectReason(), "");

        // Check that mempool size hasn't changed.
        BOOST_CHECK_EQUAL(mempool.size(), initialPoolSize + 6);
    }
}

BOOST_FIXTURE_TEST_CASE(package_orphan_tx_tests, PackageTestSetup)
{
    PackageValidationState validationState;
    CValidationState state, state2;
    CTxMempoolAcceptanceOptions opt;
    opt.context = ValidationContext::PACKAGE;

    CKey spend_key;
    spend_key.MakeNewKey(true);
    CScript spend_script = GetScriptForDestination(spend_key.GetPubKey().GetID());

    unsigned int initialPoolSize = mempool.size();
    unsigned long index_cb = m_coinbase_txns.size();//init this index before refilling coinbase

    refillCoinbase(50);

    // Parent in orphan pool
    {
        ++index_cb;
        std::vector<unsigned char> vchSig;

        COutPoint spend_cb(m_coinbase_txns[index_cb]->GetHashMalFix(), 0);
        auto mtx_parent = CreateValidTransaction(spend_cb, CAmount(49 * COIN), {CScript() << OP_TRUE << OP_EQUAL});
        Sign(vchSig, coinbaseKey, m_coinbase_txns[index_cb]->vout[0].scriptPubKey, 0, mtx_parent, 0);
        mtx_parent.vin[0].scriptSig = CScript() << vchSig;
        auto tx_parent{MakeTransactionRef(mtx_parent)};

        COutPoint spend_parent(mtx_parent.GetHashMalFix(), 0);
        auto mtx_orphan = CreateValidTransaction(spend_parent, CAmount(48 * COIN), spend_script);
        mtx_orphan.vin[0].scriptSig = CScript() << OP_TRUE;
        auto tx_orphan{MakeTransactionRef(mtx_orphan)};

        //This is the orphan tx as its parent is not known yet
        {
            LOCK(cs_main);
            CTxMempoolAcceptanceOptions opt;
            BOOST_CHECK(!AcceptToMemoryPool(tx_orphan, opt));
            BOOST_CHECK_EQUAL(opt.missingInputs.size(), 1);
        }

        COutPoint spend_child(mtx_orphan.GetHashMalFix(), 0);
        auto mtx_gchild = CreateValidTransaction(spend_child, CAmount(47 * COIN), spend_script);
        Sign(vchSig, spend_key, mtx_orphan.vout[0].scriptPubKey, 0, mtx_gchild, 0);
        mtx_gchild.vin[0].scriptSig = CScript() << vchSig;
        //mtx_gchild.vin[0].scriptSig = CScript() << OP_TRUE;
        auto tx_gchild{MakeTransactionRef(mtx_gchild)};

        // orphan is in the package as parent transaction
        Package package_parent_child{tx_orphan, tx_gchild};

        bool success = SubmitPackageToMempool(package_parent_child, state, validationState, opt);

        BOOST_CHECK(success == false);

        BOOST_CHECK_EQUAL(state.GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "");
        BOOST_CHECK_EQUAL(validationState[mtx_orphan.GetHashMalFix()].GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(validationState[mtx_orphan.GetHashMalFix()].GetRejectReason(), "");
        BOOST_CHECK(validationState[mtx_orphan.GetHashMalFix()].missingInputs);
        BOOST_CHECK_EQUAL(validationState[mtx_gchild.GetHashMalFix()].GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(validationState[mtx_gchild.GetHashMalFix()].GetRejectReason(), "");
        BOOST_CHECK(validationState[mtx_gchild.GetHashMalFix()].missingInputs);

        // Check that mempool size hasn't changed.
        BOOST_CHECK_EQUAL(mempool.size(), initialPoolSize);
    }

    // Child in orphan pool
    {
        ++index_cb;
        std::vector<unsigned char> vchSig;

        COutPoint spend_cb(m_coinbase_txns[index_cb]->GetHashMalFix(), 0);
        auto mtx_parent = CreateValidTransaction(spend_cb, CAmount(49 * COIN), {CScript() << OP_TRUE << OP_EQUAL});
        Sign(vchSig, coinbaseKey, m_coinbase_txns[index_cb]->vout[0].scriptPubKey, 0, mtx_parent, 0);
        mtx_parent.vin[0].scriptSig = CScript() << vchSig;

        //this tx has so many outputs as workaround for "tx-size-small" error when only one/two outputs are given
        COutPoint spend_parent(mtx_parent.GetHashMalFix(), 0);
        auto mtx_child = CreateValidTransaction(spend_parent, CAmount(44 * COIN), {CScript() << OP_TRUE << OP_EQUAL});
        for(auto x : {0,1,2,3})
            mtx_child.vout.push_back(CTxOut(CAmount(1 * COIN), {CScript() << OP_TRUE << OP_EQUAL}));
        mtx_child.vin[0].scriptSig = CScript() << OP_TRUE;

        COutPoint spend_child(mtx_child.GetHashMalFix(), 0);
        auto mtx_orphan = CreateValidTransaction(spend_child, CAmount(39 * COIN), spend_script);
        mtx_orphan.vin[0].scriptSig = CScript() << OP_TRUE;

        //This is the orphan tx as its parent is not known yet
        {
            LOCK(cs_main);
            auto tx_orphan{MakeTransactionRef(mtx_orphan)};
            CTxMempoolAcceptanceOptions opt;
            BOOST_CHECK(!AcceptToMemoryPool(tx_orphan, opt));
            BOOST_CHECK_EQUAL(opt.missingInputs.size(), 1);
        }

        // orphan is now in the package as child transaction.
        // no error as the parent is now known from the package
        Package package_parent_child{MakeTransactionRef(mtx_parent), MakeTransactionRef(mtx_child), MakeTransactionRef(mtx_orphan)};
        validationState.clear();

        bool success = SubmitPackageToMempool(package_parent_child, state, validationState, opt);

        BOOST_CHECK(success == true);
        BOOST_CHECK_EQUAL(state.GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "");
        BOOST_CHECK_EQUAL(validationState[mtx_parent.GetHashMalFix()].GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(validationState[mtx_parent.GetHashMalFix()].GetRejectReason(), "");
        BOOST_CHECK(!validationState[mtx_parent.GetHashMalFix()].missingInputs);
        BOOST_CHECK_EQUAL(validationState[mtx_child.GetHashMalFix()].GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(validationState[mtx_child.GetHashMalFix()].GetRejectReason(), "");
        BOOST_CHECK(!validationState[mtx_child.GetHashMalFix()].missingInputs);
        BOOST_CHECK_EQUAL(validationState[mtx_orphan.GetHashMalFix()].GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(validationState[mtx_orphan.GetHashMalFix()].GetRejectReason(), "");
        BOOST_CHECK(!validationState[mtx_orphan.GetHashMalFix()].missingInputs);

        // Check that mempool size hasn't changed.
        BOOST_CHECK_EQUAL(mempool.size(), initialPoolSize + 3);
    }
}
BOOST_FIXTURE_TEST_CASE(noncontextual_package_tests, PackageTestSetup)
{
    // The signatures won't be verified so we can just use a placeholder
    CKey placeholder_key;
    placeholder_key.MakeNewKey(true);
    CScript spk = GetScriptForDestination(placeholder_key.GetPubKey().GetID());
    CKey placeholder_key_2;
    placeholder_key_2.MakeNewKey(true);
    CScript spk2 = GetScriptForDestination(placeholder_key_2.GetPubKey().GetID());

    unsigned int initialPoolSize = mempool.size();
    unsigned long index_cb = m_coinbase_txns.size();//init this index before refilling coinbase

    refillCoinbase(50);


    // Parent and Child Package
    {
        ++index_cb;
        COutPoint spend_coinbase(m_coinbase_txns[index_cb]->GetHashMalFix(), 0);
        auto mtx_parent = CreateValidMempoolTransaction(mempool, spend_coinbase, CAmount(49 * COIN), spk);
        const CTransactionRef tx_parent{MakeTransactionRef(mtx_parent)};

        COutPoint spend_parent(tx_parent->GetHashMalFix(), 0);
        auto mtx_child = CreateValidMempoolTransaction(mempool, spend_parent, CAmount(48 * COIN), spk2);

        const CTransactionRef tx_child{MakeTransactionRef(mtx_child)};
        CValidationState state;
        BOOST_CHECK(CheckPackage({tx_parent, tx_child}, state));
        BOOST_CHECK(!CheckPackage({tx_child, tx_parent}, state));
        BOOST_CHECK_EQUAL(state.GetRejectCode(), REJECT_PACKAGE_INVALID);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "package-not-sorted");
        BOOST_CHECK(IsChildWithParents({tx_parent, tx_child}));
        BOOST_CHECK(IsChildWithParentsTree({tx_parent, tx_child}));
    }

    // 24 Parents and 1 Child
    {
        ++index_cb;
        auto final_index = index_cb + 24;
        Package package;
        CMutableTransaction child;
        for (unsigned long i{index_cb}; i < final_index; ++i, ++index_cb) {
            COutPoint spend_cb(m_coinbase_txns[index_cb]->GetHashMalFix(), 0);
            auto tx_parent = CreateValidTransaction(spend_cb, COIN, {CScript() << OP_TRUE});
            package.emplace_back(MakeTransactionRef(tx_parent));
            child.vin.emplace_back(COutPoint(tx_parent.GetHashMalFix(), 0));
        }
        child.vout.emplace_back(47 * COIN, spk2);

        CValidationState state;
        package.emplace_back(MakeTransactionRef(child));
        BOOST_CHECK(CheckPackage(package, state));
        BOOST_CHECK_EQUAL(state.GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "");

        // The parents can be in any order.
        FastRandomContext rng;
        package.pop_back();
        Shuffle(package.begin(), package.end(), rng);
        package.emplace_back(MakeTransactionRef(child));

        BOOST_CHECK(CheckPackage(package, state));
        BOOST_CHECK_EQUAL(state.GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "");
        BOOST_CHECK(IsChildWithParents(package));
        BOOST_CHECK(IsChildWithParentsTree(package));

        package.erase(package.begin());
        BOOST_CHECK(IsChildWithParents(package));
        BOOST_CHECK(CheckPackage(package, state));
        BOOST_CHECK_EQUAL(state.GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "");

        package.insert(package.begin(), m_coinbase_txns[index_cb-24]);
        BOOST_CHECK(!IsChildWithParents(package));
        BOOST_CHECK(CheckPackage(package, state));
        BOOST_CHECK_EQUAL(state.GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "");
    }

    // 2 Parents and 1 Child where one parent depends on the other.
    {
        ++index_cb;
        CMutableTransaction mtx_parent;
        mtx_parent.vin.emplace_back(COutPoint(m_coinbase_txns[index_cb]->GetHashMalFix(), 0));
        mtx_parent.vout.emplace_back(20 * COIN, spk);
        mtx_parent.vout.emplace_back(20 * COIN, spk2);
        CTransactionRef tx_parent{MakeTransactionRef(mtx_parent)};

        CMutableTransaction mtx_parent_also_child;
        mtx_parent_also_child.vin.emplace_back(COutPoint(tx_parent->GetHashMalFix(), 0));
        mtx_parent_also_child.vout.emplace_back(20 * COIN, spk);
        CTransactionRef tx_parent_also_child{MakeTransactionRef(mtx_parent_also_child)};

        CMutableTransaction mtx_child;
        mtx_child.vin.emplace_back(COutPoint(tx_parent->GetHashMalFix(), 1));
        mtx_child.vin.emplace_back(COutPoint(tx_parent_also_child->GetHashMalFix(), 0));
        mtx_child.vout.emplace_back(39 * COIN, spk);
        CTransactionRef tx_child{MakeTransactionRef(mtx_child)};

        CValidationState state;
        BOOST_CHECK(CheckPackage({tx_parent, tx_parent_also_child}, state));
        BOOST_CHECK(CheckPackage({tx_parent, tx_child}, state));
        BOOST_CHECK(CheckPackage({tx_parent, tx_parent_also_child, tx_child}, state));
        BOOST_CHECK(!CheckPackage({tx_parent_also_child, tx_parent, tx_child}, state));
        BOOST_CHECK_EQUAL(state.GetRejectCode(), REJECT_PACKAGE_INVALID);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "package-not-sorted");
    }
    BOOST_CHECK_EQUAL(mempool.size(), initialPoolSize + 2);
}

BOOST_FIXTURE_TEST_CASE(package_submission_tests, PackageTestSetup)
{
    CKey parent_key;
    parent_key.MakeNewKey(true);
    CScript parent_locking_script = GetScriptForDestination(parent_key.GetPubKey().GetID());

    unsigned int expected_pool_size = mempool.size();
    unsigned long index_cb = m_coinbase_txns.size(); //init this index before refilling coinbase

    refillCoinbase(50);

    // Unrelated transactions are not allowed in package submission.
    Package package_unrelated;
    CValidationState state, stateSubmit;
    PackageValidationState packageState;
    CTxMempoolAcceptanceOptions opt;
    opt.context = ValidationContext::PACKAGE;
    opt.nAbsurdFee = 1 * COIN;

    auto final_index = index_cb + 10;
    std::vector<unsigned char> vchSig;
    for (unsigned long i{index_cb}; i < final_index; ++i, ++index_cb) {
        COutPoint spend_coinbase(m_coinbase_txns[index_cb]->GetHashMalFix(), 0);
        CMutableTransaction mtx_parent = CreateValidTransaction(spend_coinbase, CAmount(49 * COIN), {CScript() << OP_TRUE});
        Sign(vchSig, coinbaseKey, m_coinbase_txns[index_cb]->vout[0].scriptPubKey, 0, mtx_parent, 0);
        mtx_parent.vin[0].scriptSig = CScript() << vchSig;
        package_unrelated.emplace_back(MakeTransactionRef(mtx_parent));

    }
    packageState.clear();
    auto result_unrelated = SubmitPackageToMempool(package_unrelated, state, packageState, opt);

    // We don't expect m_tx_results for each transaction when basic sanity checks haven't passed.
    BOOST_CHECK(result_unrelated);
    BOOST_CHECK(state.IsValid());
    BOOST_CHECK_EQUAL(state.GetRejectCode(), 0);
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "");
    for(auto& s : packageState) {
        BOOST_CHECK_EQUAL(s.second.GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(s.second.GetRejectReason(), "");
    }

    // Parent and Child (and Grandchild) Package
    ++index_cb;
    Package package_parent_child;
    Package package_3gen;
    COutPoint spend_cbase(m_coinbase_txns[index_cb]->GetHashMalFix(), 0);
    CMutableTransaction mtx_parent = CreateValidTransaction(spend_cbase, CAmount(49 * COIN), {CScript() << OP_TRUE << OP_EQUAL});
    Sign(vchSig, coinbaseKey, m_coinbase_txns[index_cb]->vout[0].scriptPubKey, 0, mtx_parent, 0);
    mtx_parent.vin[0].scriptSig = CScript() << vchSig;
    CTransactionRef tx_parent{MakeTransactionRef(mtx_parent)};
    package_parent_child.push_back(tx_parent);
    package_3gen.push_back(tx_parent);

    CKey child_key;
    child_key.MakeNewKey(true);
    CScript child_locking_script = GetScriptForDestination(child_key.GetPubKey().GetID());
    COutPoint spend_parent(tx_parent->GetHashMalFix(), 0);
    CMutableTransaction mtx_child = CreateValidTransaction(spend_parent, CAmount(44 * COIN), {CScript() << OP_TRUE << OP_EQUAL});
    mtx_child.vin[0].scriptSig = CScript() << OP_TRUE;
    for(auto x : {0,1,2,3})
        mtx_child.vout.push_back(CTxOut(CAmount(1 * COIN), {CScript() << OP_TRUE << OP_EQUAL}));
    CTransactionRef tx_child{MakeTransactionRef(mtx_child)};
    package_parent_child.push_back(tx_child);
    package_3gen.push_back(tx_child);

    COutPoint spend_child(tx_child->GetHashMalFix(), 0);
    CMutableTransaction mtx_grandchild = CreateValidTransaction(spend_child, CAmount(39 * COIN), CScript() << OP_TRUE << OP_EQUAL );
    mtx_grandchild.vin[0].scriptSig = CScript() << OP_TRUE;
    for(auto x : {0,1,2,3})
        mtx_grandchild.vout.push_back(CTxOut(CAmount(1 * COIN), {CScript() << OP_TRUE << OP_EQUAL}));
    CTransactionRef tx_grandchild{MakeTransactionRef(mtx_grandchild)};
    package_3gen.push_back(tx_grandchild);

    // 3 Generations is allowed.
    {
        packageState.clear();
        auto result_3gen_submit = SubmitPackageToMempool(package_3gen, state, packageState, opt);
        BOOST_CHECK(result_3gen_submit);
        BOOST_CHECK(state.IsValid());
        BOOST_CHECK_EQUAL(state.GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "");
        
        BOOST_CHECK_EQUAL(packageState[tx_parent->GetHashMalFix()].GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(packageState[tx_parent->GetHashMalFix()].GetRejectReason(), "");
        BOOST_CHECK_EQUAL(packageState[tx_child->GetHashMalFix()].GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(packageState[tx_child->GetHashMalFix()].GetRejectReason(), "");
        BOOST_CHECK_EQUAL(packageState[tx_grandchild->GetHashMalFix()].GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(packageState[tx_grandchild->GetHashMalFix()].GetRejectReason(), "");
    }

    {
        packageState.clear();
        const auto result_missing_parent = SubmitPackageToMempool(package_parent_child, state, packageState, opt);
        BOOST_CHECK(!result_missing_parent);
        BOOST_CHECK(state.IsValid());
        BOOST_CHECK_EQUAL(state.GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "");
        BOOST_CHECK_EQUAL(packageState[tx_parent->GetHashMalFix()].GetRejectCode(), REJECT_DUPLICATE);
        BOOST_CHECK_EQUAL(packageState[tx_parent->GetHashMalFix()].GetRejectReason(), "txn-already-in-mempool");
        BOOST_CHECK_EQUAL(packageState[tx_child->GetHashMalFix()].GetRejectCode(), REJECT_DUPLICATE);
        BOOST_CHECK_EQUAL(packageState[tx_child->GetHashMalFix()].GetRejectReason(), "txn-already-in-mempool");
    }
    // Parent and child package with unknown inputs
    {
        ++index_cb;
        CMutableTransaction mtx_parent_invalid = CreateValidTransaction(spend_cbase, CAmount(49 * COIN), {CScript() << OP_TRUE << OP_EQUAL});
        mtx_parent_invalid.vin[0].prevout.hashMalFix = InsecureRand256();
        CTransactionRef tx_parent_invalid{MakeTransactionRef(mtx_parent_invalid)};
        Package package_invalid_parent{tx_parent_invalid, tx_child};
        packageState.clear();
        auto result_quit_early = SubmitPackageToMempool(package_invalid_parent, state, packageState, opt);
        BOOST_CHECK(!result_quit_early);
        BOOST_CHECK(state.IsValid());
        BOOST_CHECK_EQUAL(state.GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "");
        BOOST_CHECK_EQUAL(packageState[tx_parent_invalid->GetHashMalFix()].GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(packageState[tx_parent_invalid->GetHashMalFix()].GetRejectReason(), "");
        BOOST_CHECK_EQUAL(packageState[tx_parent_invalid->GetHashMalFix()].missingInputs, true);
        BOOST_CHECK_EQUAL(packageState[tx_child->GetHashMalFix()].GetRejectCode(), REJECT_DUPLICATE);
        BOOST_CHECK_EQUAL(packageState[tx_child->GetHashMalFix()].GetRejectReason(), "txn-already-in-mempool");
    }

    // high fee tx.
    {
        opt.nAbsurdFee = 0.5 * COIN;

        ++index_cb;
        COutPoint spend_cbase(m_coinbase_txns[index_cb]->GetHashMalFix(), 0);
        CMutableTransaction mtx_parent = CreateValidTransaction(spend_cbase, CAmount(49 * COIN), {CScript() << OP_TRUE << OP_EQUAL});
        Sign(vchSig, coinbaseKey, m_coinbase_txns[index_cb]->vout[0].scriptPubKey, 0, mtx_parent, 0);
        mtx_parent.vin[0].scriptSig = CScript() << vchSig;
        CTransactionRef tx_parent{MakeTransactionRef(mtx_parent)};

        COutPoint spend_parent(tx_parent->GetHashMalFix(), 0);
        CMutableTransaction mtx_child2 = CreateValidTransaction(spend_parent, CAmount(5 * COIN), {CScript() << OP_TRUE << OP_EQUAL});
        mtx_child2.vin[0].scriptSig = CScript() << OP_TRUE;
        mtx_child2.vout.push_back(CTxOut(CAmount(1 * COIN), {CScript() << OP_TRUE << OP_EQUAL}));
        CTransactionRef tx_child2{MakeTransactionRef(mtx_child2)};

        Package package_high_fee{tx_parent, tx_child2};
        packageState.clear();

        const auto result_missing_parent = SubmitPackageToMempool(package_high_fee, state, packageState, opt);

        BOOST_CHECK(!result_missing_parent);
        BOOST_CHECK(state.IsValid());
        BOOST_CHECK_EQUAL(state.GetRejectCode(), 0);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "");
        BOOST_CHECK_EQUAL(packageState[tx_parent->GetHashMalFix()].GetRejectCode(), REJECT_HIGHFEE);
        BOOST_CHECK_EQUAL(packageState[tx_parent->GetHashMalFix()].GetRejectReason(), "absurdly-high-fee");
        BOOST_CHECK_EQUAL(packageState[tx_parent->GetHashMalFix()].missingInputs, false);
        BOOST_CHECK_EQUAL(packageState[tx_child2->GetHashMalFix()].GetRejectCode(), REJECT_NONSTANDARD);
        BOOST_CHECK_EQUAL(packageState[tx_child2->GetHashMalFix()].GetRejectReason(), "tx-size-small");
    }

}

BOOST_AUTO_TEST_SUITE_END()
