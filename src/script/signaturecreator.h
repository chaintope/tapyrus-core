// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_SCRIPT_SIGNATURECREATOR_H
#define TAPYRUS_SCRIPT_SIGNATURECREATOR_H

#include <amount.h>
#include <pubkey.h>
#include <script/interpreter.h>
#include <script/transactionsignaturechecker.h>

#include <vector>

class CKeyID;
class CScript;
class SigningProvider;
struct CMutableTransaction;

/** Interface for signature creators. */
class BaseSignatureCreator {
public:
    virtual ~BaseSignatureCreator() {}
    virtual const BaseSignatureChecker& Checker() const =0;

    /** Create a singular (non-script) signature. */
    virtual bool CreateSig(const SigningProvider& provider, std::vector<unsigned char>& vchSig, const CKeyID& keyid, const CScript& scriptCode) const =0;
};

/** A signature creator for transactions. */
class MutableTransactionSignatureCreator : public BaseSignatureCreator {
    const CMutableTransaction* txTo;
    unsigned int nIn;
    int nHashType;
    CAmount amount;
    const MutableTransactionSignatureChecker checker;
    SignatureScheme sigScheme;

public:
    MutableTransactionSignatureCreator(const CMutableTransaction* txToIn, unsigned int nInIn, const CAmount& amountIn, int nHashTypeIn = SIGHASH_ALL, const SignatureScheme sigScheme = SignatureScheme::ECDSA);
    const BaseSignatureChecker& Checker() const override { return checker; }
    bool CreateSig(const SigningProvider& provider, std::vector<unsigned char>& vchSig, const CKeyID& keyid, const CScript& scriptCode) const override;
};

/** A signature creator that just produces 71-byte empty signatures. */
extern const BaseSignatureCreator& DUMMY_SIGNATURE_CREATOR;
/** A signature creator that just produces 72-byte empty signatures. */
extern const BaseSignatureCreator& DUMMY_MAXIMUM_SIGNATURE_CREATOR;

#endif // TAPYRUS_SCRIPT_SIGNATURECREATOR_H
