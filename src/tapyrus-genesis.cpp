// Copyright (c) 2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <clientversion.h>
#include <util.h>
#include <chainparamsbase.h>
#include <chainparams.h>
#include <utilstrencodings.h>
#include <key_io.h>
#include <streams.h>
#include <consensus/validation.h>
#include <validation.h>
#include <tinyformat.h>

#include <stdio.h>

static bool fCreateBlank;
static const int CONTINUE_EXECUTION=-1;

static void SetupTapyrusGenesisArgs()
{
    gArgs.AddArg("-?", "This help message", false, OptionsCategory::OPTIONS);
    gArgs.AddArg("-create", "Create new genesis block.", false, OptionsCategory::OPTIONS);
    SetupChainParamsBaseOptions();

    // Signed Blocks options
    gArgs.AddArg("-signblockpubkey=<pubkey>", "Optional. Sets the aggregate public key for Signed Blocks", false, OptionsCategory::SIGN_BLOCK);
    gArgs.AddArg("-signblockprivatekey=<privatekey-WIF>", "Optional. Sets the aggregate private key in WIF to be used to sign genesis block. If it is not set, this command create no proof in genesis block.", false, OptionsCategory::SIGN_BLOCK);

    // Genesis Block options
    gArgs.AddArg("-time=<time>", "Specify genesis block time as UNIX Time. If this don't set, use current time.", false, OptionsCategory::GENESIS);
    gArgs.AddArg("-toaddress=<address>", "Specify coinbase script pay to address.", false, OptionsCategory::GENESIS);

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

    // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
    try {
        SelectParams(gArgs.GetChainName());
    } catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return EXIT_FAILURE;
    }

    fCreateBlank = gArgs.GetBoolArg("-create", false);

    if (argc < 2 || HelpRequested(gArgs)) {
        // First part of help message is specific to this utility
        std::string strUsage = PACKAGE_NAME " tapyrus-genesis utility version " + FormatFullVersion() + "\n\n" +
                               "Usage:  tapyrus-genesis [options] <hex-tx> [commands]  Update hex-encoded bitcoin transaction\n" +
                               "or:     tapyrus-genesis [options] -create [commands]   Create hex-encoded bitcoin transaction\n" +
                               "\n";
        strUsage += gArgs.GetHelpMessage();

        fprintf(stdout, "%s", strUsage.c_str());

        if (argc < 2) {
            fprintf(stderr, "Error: too few parameters\n");
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    return CONTINUE_EXECUTION;
}

static int CommandLine()
{
    CKey privatekey(DecodeSecret(gArgs.GetArg("-signblockprivatekey", "")));

    auto blockTime = gArgs.GetArg("-time", 0);
    if(!blockTime) {
        blockTime = time(0);
    }

    auto toAddress = gArgs.GetArg("-toaddress", "");
    if(!IsValidDestinationString(toAddress)) {
        fprintf(stderr, "Error: Invalid address specified in -toaddress option.\n");
        return EXIT_FAILURE;
    }

    // This is for using CKey.sign().
    ECC_Start();

    // This is for using CPubKey.verify().
    ECCVerifyHandle globalVerifyHandle;

    CBlock genesis { createGenesisBlock(privatekey.IsValid() ? privatekey.GetPubKey() : GetAggregatePubkeyFromCmdLine(), privatekey, blockTime) };

    // check validity
    CValidationState state;
    bool fCheckProof = privatekey.IsValid();
    if(!CheckBlock(genesis, state, Params().GetConsensus(), fCheckProof)) {
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