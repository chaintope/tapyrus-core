#include <consensus/signedblock.h>
#include <util.h>
#include <utilstrencodings.h>

std::unique_ptr<MultisigCondition> MultisigCondition::instance(nullptr);

const MultisigCondition& MultisigCondition::getInstance()
{
    if(!instance.get())
        throw std::runtime_error(strprintf("%s: called before CreateChainParams.", __func__));
    return *instance;
}

void MultisigCondition::ParsePubkeyString(std::string source)
{
    std::string prefix;
    std::string pubkeyString;

    pubkeys.clear();

    for (unsigned int i = 0; i < source.length();) {
        prefix = source.substr(i, 2);

        if (prefix == "02" || prefix == "03") {
            pubkeyString = source.substr(i, 66);
            i += 66;
        } else if(prefix == "04" || prefix == "06" || prefix == "07") {
            throw std::runtime_error(strprintf("Uncompressed public key format are not acceptable: %s", source));
        } else {
            throw std::runtime_error(strprintf("Public Keys for Signed Block include invalid pubkey: %s", pubkeyString));
        }

        std::vector<unsigned char> vch = ParseHex(pubkeyString);
        CPubKey pubkey(vch.begin(), vch.end());

        if(!pubkey.IsFullyValid()) {
            throw std::runtime_error(strprintf("Public Keys for Signed Block include invalid pubkey: %s", pubkeyString));
        }

        pubkeys.push_back(pubkey);
    }

    // sort as ascending order
    std::sort(pubkeys.begin(), pubkeys.end());
}

MultisigCondition::MultisigCondition(const std::string& pubkeyString, const int threshold)
{
    if(instance && instance->pubkeys.size() && instance->threshold && (unsigned long)instance->threshold <= instance->pubkeys.size() && instance->pubkeys.size() <= SIGNED_BLOCKS_MAX_KEY_SIZE)
        return;

    if(!instance)
        instance.reset(new MultisigCondition());

    instance->ParsePubkeyString(pubkeyString);
    instance->threshold = threshold;

    if (!instance->pubkeys.size()) {
        throw std::runtime_error(strprintf("Invalid or empty publicKeyString"));
    }

    if (instance->pubkeys.size() > SIGNED_BLOCKS_MAX_KEY_SIZE) {
        throw std::runtime_error(strprintf("Public Keys for Signed Block are up to %d, but passed %d.", SIGNED_BLOCKS_MAX_KEY_SIZE, instance->pubkeys.size()));
    }

    if (instance->threshold < 1 || (unsigned int)instance->threshold > instance->pubkeys.size()) {
        throw std::runtime_error(strprintf("Threshold can be between 1 to %d, but passed %d.", instance->pubkeys.size(), instance->threshold));
    }
}

