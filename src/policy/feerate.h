// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POLICY_FEERATE_H
#define BITCOIN_POLICY_FEERATE_H

#include <amount.h>
#include <serialize.h>

#include <string>

extern const std::string CURRENCY_UNIT;

/**
 * Fee rate in tapyrus per kilobyte: CAmount / kB
 */
class CFeeRate
{
private:
    CAmount nTapyrusPerk; // unit is tapyrus-per-1,000-bytes

public:
    /** Fee rate of 0 tapyrus per kB */
    CFeeRate() : nTapyrusPerk(0) { }
    template<typename I>
    CFeeRate(const I _nTapyrusPerk): nTapyrusPerk(_nTapyrusPerk) {
        // We've previously had bugs creep in from silent double->int conversion...
        static_assert(std::is_integral<I>::value, "CFeeRate should be used without floats");
    }
    /** Constructor for a fee rate in tapyrus per kB. The size in bytes must not exceed (2^63 - 1)*/
    CFeeRate(const CAmount& nFeePaid, size_t nBytes);
    /**
     * Return the fee in tapyrus for the given size in bytes.
     */
    CAmount GetFee(size_t nBytes) const;
    /**
     * Return the fee in tapyrus for a size of 1000 bytes
     */
    CAmount GetFeePerK() const { return GetFee(1000); }
    friend bool operator<(const CFeeRate& a, const CFeeRate& b) { return a.nTapyrusPerk < b.nTapyrusPerk; }
    friend bool operator>(const CFeeRate& a, const CFeeRate& b) { return a.nTapyrusPerk > b.nTapyrusPerk; }
    friend bool operator==(const CFeeRate& a, const CFeeRate& b) { return a.nTapyrusPerk == b.nTapyrusPerk; }
    friend bool operator<=(const CFeeRate& a, const CFeeRate& b) { return a.nTapyrusPerk <= b.nTapyrusPerk; }
    friend bool operator>=(const CFeeRate& a, const CFeeRate& b) { return a.nTapyrusPerk >= b.nTapyrusPerk; }
    friend bool operator!=(const CFeeRate& a, const CFeeRate& b) { return a.nTapyrusPerk != b.nTapyrusPerk; }
    CFeeRate& operator+=(const CFeeRate& a) { nTapyrusPerk += a.nTapyrusPerk; return *this; }
    std::string ToString() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nTapyrusPerk);
    }
};

#endif //  BITCOIN_POLICY_FEERATE_H
