// Copyright (c) 2019 Chaintope Inc.
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
        FederationParams().ReadAggregatePubkey(ParseHex(gArgs.GetArg("-signblockpubkey", "")), 0);
    } catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return EXIT_FAILURE;
    }

    return CONTINUE_EXECUTION;
}

static int CommandLine()
{
    std::string wif = gArgs.GetArg("-signblockprivatekey", "");
    CKey privatekey(DecodeSecret(wif));
    if(wif.size() && !privatekey.IsValid())
    {
        fprintf(stderr, "Error: Aggregate private key was invalid.\n");
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

    // This is for using CKey.sign().
    ECC_Start();

    // This is for using CPubKey.verify().
    ECCVerifyHandle globalVerifyHandle;

    CBlock genesis { createGenesisBlock(FederationParams().GetLatestAggregatePubkey(), privatekey, blockTime, payToAddress) };

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