// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_INTERFACES_WALLET_H
#define BITCOIN_INTERFACES_WALLET_H

#include <amount.h>                    // For CAmount
#include <pubkey.h>                    // For CKeyID and CScriptID (definitions needed in CTxDestination instantiation)
#include <script/ismine.h>             // For isminefilter, isminetype
#include <script/standard.h>           // For CTxDestination
#include <support/allocators/secure.h> // For SecureString
#include <ui_interface.h>              // For ChangeType

#include <functional>
#include <map>
#include <memory>
#include <stdint.h>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

class CCoinControl;
class CFeeRate;
class CKey;
class CWallet;
enum class FeeReason;
enum class OutputType;
struct CRecipient;

namespace interfaces {

class Handler;
class PendingWalletTx;
struct WalletAddress;
struct WalletBalances;
struct WalletTx;
struct WalletTxOut;
struct WalletTxStatus;

using WalletOrderForm = std::vector<std::pair<std::string, std::string>>;
using WalletValueMap = std::map<std::string, std::string>;

//! Interface for accessing a wallet.
class Wallet
{
public:
    virtual ~Wallet() {}

    //! Encrypt wallet.
    virtual bool encryptWallet(const SecureString& wallet_passphrase) = 0;

    //! Return whether wallet is encrypted.
    virtual bool isCrypted() = 0;

    //! Lock wallet.
    virtual bool lock() = 0;

    //! Unlock wallet.
    virtual bool unlock(const SecureString& wallet_passphrase) = 0;

    //! Return whether wallet is locked.
    virtual bool isLocked() = 0;

    //! Change wallet passphrase.
    virtual bool changeWalletPassphrase(const SecureString& old_wallet_passphrase,
        const SecureString& new_wallet_passphrase) = 0;

    //! Abort a rescan.
    virtual void abortRescan() = 0;

    //! Back up wallet.
    virtual bool backupWallet(const std::string& filename) = 0;

    //! Get wallet name.
    virtual std::string getWalletName() = 0;

    // Get key from pool.
    virtual bool getKeyFromPool(bool internal, CPubKey& pub_key) = 0;

    //! Get public key.
    virtual bool getPubKey(const CKeyID& address, CPubKey& pub_key) = 0;

    //! Get private key.
    virtual bool getPrivKey(const CKeyID& address, CKey& key) = 0;

    //! Return whether wallet has private key.
    virtual bool isSpendable(const CTxDestination& dest) = 0;

    //! Return whether wallet has watch only keys.
    virtual bool haveWatchOnly() = 0;

    //! Add or update address.
    virtual bool setAddressBook(const CTxDestination& dest, const std::string& name, const std::string& purpose) = 0;

    // Remove address.
    virtual bool delAddressBook(const CTxDestination& dest) = 0;

    //! Look up address in wallet, return whether exists.
    virtual bool getAddress(const CTxDestination& dest,
        std::string* name,
        isminetype* is_mine,
        std::string* purpose) = 0;

    //! Get wallet address list.
    virtual std::vector<WalletAddress> getAddresses() = 0;

    //! Add dest data.
    virtual bool addDestData(const CTxDestination& dest, const std::string& key, const std::string& value) = 0;

    //! Erase dest data.
    virtual bool eraseDestData(const CTxDestination& dest, const std::string& key) = 0;

    //! Get dest values with prefix.
    virtual std::vector<std::string> getDestValues(const std::string& prefix) = 0;

    //! Lock coin.
    virtual void lockCoin(const COutPoint& output) = 0;

    //! Unlock coin.
    virtual void unlockCoin(const COutPoint& output) = 0;

    //! Return whether coin is locked.
    virtual bool isLockedCoin(const COutPoint& output) = 0;

    //! List locked coins.
    virtual void listLockedCoins(std::vector<COutPoint>& outputs) = 0;

    //! Create transaction.
    virtual std::unique_ptr<PendingWalletTx> createTransaction(const std::vector<CRecipient>& recipients,
        const CCoinControl& coin_control,
        bool sign,
        int& change_pos,
        CAmount& fee,
        std::string& fail_reason) = 0;

    //! Return whether transaction can be abandoned.
    virtual bool transactionCanBeAbandoned(const uint256& txid) = 0;

    //! Abandon transaction.
    virtual bool abandonTransaction(const uint256& txid) = 0;

    //! Return whether transaction can be bumped.
    virtual bool transactionCanBeBumped(const uint256& txid) = 0;

    //! Create bump transaction.
    virtual bool createBumpTransaction(const uint256& txid,
        const CCoinControl& coin_control,
        CAmount total_fee,
        std::vector<std::string>& errors,
        CAmount& old_fee,
        CAmount& new_fee,
        CMutableTransaction& mtx) = 0;

    //! Sign bump transaction.
    virtual bool signBumpTransaction(CMutableTransaction& mtx) = 0;

    //! Commit bump transaction.
    virtual bool commitBumpTransaction(const uint256& txid,
        CMutableTransaction&& mtx,
        std::vector<std::string>& errors,
        uint256& bumped_txid) = 0;

    //! Get a transaction.
    virtual CTransactionRef getTx(const uint256& txid) = 0;

    //! Get transaction information.
    virtual WalletTx getWalletTx(const uint256& txid) = 0;

    //! Get list of all wallet transactions.
    virtual std::vector<WalletTx> getWalletTxs() = 0;

    //! Try to get updated status for a particular transaction, if possible without blocking.
    virtual bool tryGetTxStatus(const uint256& txid,
        WalletTxStatus& tx_status,
        int& num_blocks,
        int64_t& adjusted_time) = 0;

    //! Get transaction details.
    virtual WalletTx getWalletTxDetails(const uint256& txid,
        WalletTxStatus& tx_status,
        WalletOrderForm& order_form,
        bool& in_mempool,
        int& num_blocks,
        int64_t& adjusted_time) = 0;

    //! Get balances.
    virtual WalletBalances getBalances() = 0;

    //! Get balances if possible without blocking.
    virtual bool tryGetBalances(WalletBalances& balances, int& num_blocks) = 0;

    //! Get balance.
    virtual CAmount getBalance(ColorIdentifier colorId = ColorIdentifier()) = 0;

    //! Get available balance.
    virtual CAmount getAvailableBalance(const CCoinControl& coin_control, ColorIdentifier colorId = ColorIdentifier()) = 0;

    //! Return whether transaction input belongs to wallet.
    virtual isminetype txinIsMine(const CTxIn& txin) = 0;

    //! Return whether transaction output belongs to wallet.
    virtual isminetype txoutIsMine(const CTxOut& txout) = 0;

    //! Return debit amount if transaction input belongs to wallet.
    virtual CAmount getDebit(const CTxIn& txin, isminefilter filter) = 0;

    //! Return credit amount if transaction input belongs to wallet.
    virtual CAmount getCredit(const CTxOut& txout, isminefilter filter) = 0;

    //! Return AvailableCoins + LockedCoins grouped by wallet address.
    //! (put change in one group with wallet address)
    using CoinsList = std::map<CTxDestination, std::vector<std::tuple<COutPoint, WalletTxOut>>>;
    virtual CoinsList listCoins() = 0;

    //! Return wallet transaction output information.
    virtual std::vector<WalletTxOut> getCoins(const std::vector<COutPoint>& outputs) = 0;

    //! Get required fee.
    virtual CAmount getRequiredFee(unsigned int tx_bytes) = 0;

    //! Get minimum fee.
    virtual CAmount getMinimumFee(unsigned int tx_bytes,
        const CCoinControl& coin_control,
        int* returned_target,
        FeeReason* reason) = 0;

    //! Get tx confirm target.
    virtual unsigned int getConfirmTarget() = 0;

    // Return whether HD enabled.
    virtual bool hdEnabled() = 0;

    // check if a certain wallet flag is set.
    virtual bool IsWalletFlagSet(uint64_t flag) = 0;

    // Get default address type.
    virtual OutputType getDefaultAddressType() = 0;

    // Get default change type.
    virtual OutputType getDefaultChangeType() = 0;

    //! Register handler for unload message.
    using UnloadFn = std::function<void()>;
    virtual std::unique_ptr<Handler> handleUnload(UnloadFn fn) = 0;

    //! Register handler for show progress messages.
    using ShowProgressFn = std::function<void(const std::string& title, int progress)>;
    virtual std::unique_ptr<Handler> handleShowProgress(ShowProgressFn fn) = 0;

    //! Register handler for status changed messages.
    using StatusChangedFn = std::function<void()>;
    virtual std::unique_ptr<Handler> handleStatusChanged(StatusChangedFn fn) = 0;

    //! Register handler for address book changed messages.
    using AddressBookChangedFn = std::function<void(const CTxDestination& address,
        const std::string& label,
        bool is_mine,
        const std::string& purpose,
        ChangeType status)>;
    virtual std::unique_ptr<Handler> handleAddressBookChanged(AddressBookChangedFn fn) = 0;

    //! Register handler for transaction changed messages.
    using TransactionChangedFn = std::function<void(const uint256& txid, ChangeType status)>;
    virtual std::unique_ptr<Handler> handleTransactionChanged(TransactionChangedFn fn) = 0;

    //! Register handler for watchonly changed messages.
    using WatchOnlyChangedFn = std::function<void(bool have_watch_only)>;
    virtual std::unique_ptr<Handler> handleWatchOnlyChanged(WatchOnlyChangedFn fn) = 0;
};

//! Tracking object returned by CreateTransaction and passed to CommitTransaction.
class PendingWalletTx
{
public:
    virtual ~PendingWalletTx() {}

    //! Get transaction data.
    virtual const CTransaction& get() = 0;

    //! Get virtual transaction size.
    virtual int64_t getVirtualSize() = 0;

    //! Send pending transaction and commit to wallet.
    virtual bool commit(WalletValueMap value_map,
        WalletOrderForm order_form,
        std::string from_account,
        std::string& reject_reason) = 0;
};

//! Information about one wallet address.
struct WalletAddress
{
    CTxDestination dest;
    isminetype is_mine;
    std::string name;
    std::string purpose;

    WalletAddress(CTxDestination dest, isminetype is_mine, std::string name, std::string purpose)
        : dest(std::move(dest)), is_mine(is_mine), name(std::move(name)), purpose(std::move(purpose))
    {
    }
};

//! Collection of wallet balances.
struct WalletBalances
{
    TxColoredCoinBalancesMap balances;
    TxColoredCoinBalancesMap unconfirmed_balances;
    bool have_watch_only;
    TxColoredCoinBalancesMap watch_only_balances;
    TxColoredCoinBalancesMap unconfirmed_watch_only_balances;
    std::set<ColorIdentifier> tokens;
    std::set<ColorIdentifier>::iterator tokenIndex;

    WalletBalances(){
        have_watch_only = false;
        tokenIndex = tokens.begin();
    }

    CAmount getBalance() const
    {
        auto it = balances.find(*tokenIndex);
        return it != balances.end() ? it->second : 0;
    }

    CAmount getUnconfirmedBalance() const
    {
        auto it = unconfirmed_balances.find(*tokenIndex);
        return it != unconfirmed_balances.end() ? it->second : 0;
    }

    CAmount getWatchOnlyBalance() const
    {
        auto it = watch_only_balances.find(*tokenIndex);
        return it != watch_only_balances.end() ? it->second : 0;
    }

    CAmount getUnconfirmedWatchOnlyBalance() const
    {
        auto it = unconfirmed_watch_only_balances.find(*tokenIndex);
        return it != unconfirmed_watch_only_balances.end() ? it->second : 0;
    }

    bool balanceChanged(const WalletBalances& prev) const
    {
        return balances != prev.balances || unconfirmed_balances != prev.unconfirmed_balances ||
               watch_only_balances != prev.watch_only_balances ||
               unconfirmed_watch_only_balances != prev.unconfirmed_watch_only_balances;
    }

    //collect all tokens in the wallet from all the balance lists
    void refreshTokens() {
        tokens.clear();

        for(auto pair:balances)
            tokens.insert(pair.first);
        for(auto pair:unconfirmed_balances)
            tokens.insert(pair.first);
        for(auto pair:watch_only_balances)
            tokens.insert(pair.first);
        for(auto pair:unconfirmed_watch_only_balances)
            tokens.insert(pair.first);

        tokenIndex = tokens.begin();
    }

    void prev()
    {
        if(tokenIndex != tokens.begin())
            tokenIndex--;
        else
        {
            tokenIndex = tokens.end();
            tokenIndex--;
        }
    }

    void next()
    {
        if(tokenIndex != tokens.end())
        {
            tokenIndex++;
            if(tokenIndex == tokens.end())
                tokenIndex = tokens.begin();
        }
    }

    bool isToken()
    {
        return (*tokenIndex).type != TokenTypes::NONE;
    }

    std::string getTokenName()
    {
        return (*tokenIndex).toHexString();
    }
};

// Wallet transaction information.
struct WalletTx
{
    CTransactionRef tx;
    std::vector<isminetype> txin_is_mine;
    std::vector<isminetype> txout_is_mine;
    std::vector<CTxDestination> txout_address;
    std::vector<isminetype> txout_address_is_mine;
    TxColoredCoinBalancesMap credits;
    TxColoredCoinBalancesMap debits;
    TxColoredCoinBalancesMap changes;
    int64_t time;
    std::map<std::string, std::string> value_map;
    bool is_coinbase;

    CAmount getCredit(const ColorIdentifier& colorId = ColorIdentifier()) const
    {
        auto it = credits.find(colorId);
        return it != credits.end() ? it->second : 0;
    }

    CAmount getDebit(const ColorIdentifier& colorId = ColorIdentifier()) const
    {
        auto it = debits.find(colorId);
        return it != debits.end() ? it->second : 0;
    }

    CAmount getChange(const ColorIdentifier& colorId = ColorIdentifier()) const
    {
        auto it = changes.find(colorId);
        return it != changes.end() ? it->second : 0;
    }
};

//! Updated transaction status.
struct WalletTxStatus
{
    int block_height;
    int depth_in_main_chain;
    unsigned int time_received;
    uint32_t lock_time;
    bool is_final;
    bool is_trusted;
    bool is_abandoned;
    bool is_coinbase;
    bool is_in_main_chain;
};

//! Wallet transaction output.
struct WalletTxOut
{
    CTxOut txout;
    int64_t time;
    int depth_in_main_chain = -1;
    bool is_spent = false;
};

//! Return implementation of Wallet interface. This function will be undefined
//! in builds where ENABLE_WALLET is false.
std::unique_ptr<Wallet> MakeWallet(const std::shared_ptr<CWallet>& wallet);

} // namespace interfaces

#endif // BITCOIN_INTERFACES_WALLET_H
