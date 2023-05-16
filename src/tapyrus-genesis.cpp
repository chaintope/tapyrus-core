// Copyright (c) 2019-2021 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <clientversion.h>
#include <util.h>
#include <federationparams.h>
#include <chainparams.h>
#include <utilstrencodings.h>
#include <key_io.h>
#include <streams.h>
#include <consensus/validation.h>
#include <validation.h>
#include <tinyformat.h>
#include <script/standard.h>
#include <stdio.h>
#include <xfieldhistory.h>

static const int CONTINUE_EXECUTION=-1;

static void SetupTapyrusGenesisArgs()
{
    gArgs.AddArg("-?", "This help message", false, OptionsCategory::OPTIONS);
    SetupFederationParamsOptions();

    // Signed Blocks options
    gArgs.AddArg("-signblockpubkey=<pubkey>", "Sets the aggregate public key for Signed Blocks", false, OptionsCategory::GENESIS);
    gArgs.AddArg("-signblockprivatekey=<privatekey-WIF>", "Optional. Sets the aggregate private key in WIF to be used to sign genesis block. If it is not set, this command creates no proof in genesis block.", false, OptionsCategory::GENESIS);

    // Genesis Block options
    gArgs.AddArg("-time=<time>", "Specify genesis block time as UNIX Time. If this don't set, use current time.", false, OptionsCategory::GENESIS);
    gArgs.AddArg("-address=<pay_to_address>", "Optional. Specify coinbase script pay to address.", false, OptionsCategory::GENESIS);

    // Dev mode option
    gArgs.AddArg("-dev", "Specify dev environment.", false, OptionsCategory::GENESIS);
    gArgs.AddArg("-generatekey", "Generate a public key, private key pair in dev mode.", false, OptionsCategory::GENESIS);

    // Hidden
    gArgs.AddArg("-h", "", false, OptionsCategory::HIDDEN);
    gArgs.AddArg("-help", "", false, OptionsCategory::HIDDEN);
}

//
// This function returns either one of EXIT_ codes when it's expected to stop the process or
// CONTINUE_EXECUTION when it's expected to continue further.
//
static int AppInit(int argc, char* argv[])
{
    //
    // Parameters
    //
    SetupTapyrusGenesisArgs();
    std::string error;
    if (!gArgs.ParseParameters(argc, argv, error)) {
        fprintf(stderr, "Error parsing command line arguments: %s\n", error.c_str());
        return EXIT_FAILURE;
    }

    if (argc < 2 || HelpRequested(gArgs)) {
        // First part of help message is specific to this utility
        std::string strUsage = PACKAGE_NAME " tapyrus-genesis utility version " + FormatFullVersion() + "\n\n" +
                               "Usage:   tapyrus-genesis [options]\n" +
                               "         Create hex-encoded tapyrus genesis block\n" +
                               "\n";
        strUsage += gArgs.GetHelpMessage();

        fprintf(stdout, "%s", strUsage.c_str());

        if (argc < 2) {
            fprintf(stderr, "Error: too few parameters\n");
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    // Check for -dev parameter (Params() calls are only valid after this clause)
    try {
        SelectParams(gArgs.GetChainMode());
        SelectFederationParams(gArgs.GetChainMode(), false);
    } catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return EXIT_FAILURE;
    }

    return CONTINUE_EXECUTION;
}

static int generateNewKeyPair()
{
    //generate a secret key
    CKey secret;
    secret.MakeNewKey(true);//compressed

    //generate its public key
    CPubKey pubkey = secret.GetPubKey();
    assert(pubkey.IsFullyValid());
    assert(secret.VerifyPubKey(pubkey));

    fprintf(stdout, "private key: %s\n", EncodeSecret(secret).c_str());
    fprintf(stdout, "public key: %s\n", HexStr(pubkey.begin(), pubkey.end()).c_str());

    return EXIT_SUCCESS;
}

static int generateGenesis(CPubKey aggpubkey, CKey& privatekey, long long blockTime, std::string& payToAddress)
{
    CBlock genesis { createGenesisBlock(aggpubkey, privatekey, blockTime, payToAddress) };

    //initialize xfield history
    CXFieldHistory history(genesis);

    // check validity
    CValidationState state;
    bool fCheckProof = privatekey.IsValid();
    if(!CheckBlock(genesis, state, fCheckProof)) {
        fprintf(stderr, "error: Consensus::CheckBlock: %s", FormatStateMessage(state).c_str());
        return EXIT_FAILURE;
    }

    // serialize
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << genesis;
    std::string genesis_hex = HexStr(ss.begin(), ss.end());

    // print
    fprintf(stdout, "%s\n", genesis_hex.c_str());
    return EXIT_SUCCESS;
}

static int CommandLine()
{
    auto generateKey = gArgs.GetBoolArg("-generatekey", false);
    if(generateKey && gArgs.GetChainMode() != TAPYRUS_OP_MODE::DEV)
    {
        fprintf(stderr, "Error: generateKey is supported only in DEV mode.\n");
        return EXIT_FAILURE;
    }

    CPubKey aggpubkey;
    try {
        std::string pubkey = gArgs.GetArg("-signblockpubkey", "");
        if(pubkey.size())
        {
            std::vector<unsigned char> key( ParseHex(pubkey) );
            aggpubkey = CPubKey(key.begin(), key.end());
            if(!aggpubkey.IsFullyValid())
            {
                fprintf(stderr, "Error: Aggregate Public Key was invalid.\n");
                return EXIT_FAILURE;
            }
            if(!aggpubkey.IsCompressed())
            {
                fprintf(stderr, "Error: Uncompressed Aggregate Public Keys are not supported.\n");
                return EXIT_FAILURE;
            }
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return EXIT_FAILURE;
    }

    // This is for using CKey.sign().
    ECC_Start();

    // This is for using CPubKey.verify().
    ECCVerifyHandle globalVerifyHandle;

    std::string wif = gArgs.GetArg("-signblockprivatekey", "");
    CKey privatekey(DecodeSecret(wif));
    if(wif.size() && !privatekey.IsValid())
    {
        fprintf(stderr, "Error: Aggregate private key was invalid.\n");
        return EXIT_FAILURE;
    }

    if(privatekey.IsValid() && aggpubkey != privatekey.GetPubKey())
    {
        fprintf(stderr, "Error: Aggregate private key does not correspond to given Aggregate public key.\n");
        return EXIT_FAILURE;
    }

    auto blockTime = gArgs.GetArg("-time", 0);
    if(!blockTime) {
        blockTime = time(0);
    }

    auto payToAddress = gArgs.GetArg("-address", "");
     if(payToAddress.size() && !IsValidDestinationString(payToAddress)) {
         fprintf(stderr, "Error: Invalid address specified in -address option.\n");
         return EXIT_FAILURE;
     }

    //generate after ECC initialization
    if(generateKey)
        return generateNewKeyPair();
    else
        return generateGenesis(aggpubkey, privatekey, blockTime, payToAddress);
}

int main(int argc, char* argv[])
{
    SetupEnvironment();

    try {
        int ret = AppInit(argc, argv);
        if (ret != CONTINUE_EXECUTION)
            return ret;
    }
    catch (const std::exception& e) {
        PrintExceptionContinue(&e, "AppInit()");
        return EXIT_FAILURE;
    } catch (...) {
        PrintExceptionContinue(nullptr, "AppInit()");
        return EXIT_FAILURE;
    }

    int ret = EXIT_FAILURE;
    try {
        ret = CommandLine();
    }
    catch (const std::exception& e) {
        PrintExceptionContinue(&e, "CommandLine()");
    } catch (...) {
        PrintExceptionContinue(nullptr, "CommandLine()");
    }
    return ret;
}