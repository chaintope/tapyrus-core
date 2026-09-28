// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for ProcessMessage (src/net_processing.cpp) -- the dispatcher
// for every P2P message a peer can send. ProcessMessage itself is static,
// so this drives it the way CConnman's message handler thread does: one
// wire-format message is queued on a handshaken CNode, then
// PeerLogicValidation::ProcessMessages() and SendMessages() run against
// the shared FuzzNodeSetup node context (see fuzz_node_setup.h).
//
// The message header is built here, with this network's magic bytes and a
// correct checksum, so fuzz input isn't spent on the magic/checksum
// rejects in ProcessMessages(). The command is usually one of
// getAllNetMessageTypes(), otherwise an arbitrary string of up to
// COMMAND_SIZE bytes; the payload is the remaining input.

#include <tapyrus-config.h>

#include <federationparams.h>
#include <hash.h>
#include <net.h>
#include <net_processing.h>
#include <netaddress.h>
#include <protocol.h>
#include <streams.h>
#include <sync.h>
#include <utiltime.h>
#include <version.h>

#include <test/fuzz/fuzz_code/FuzzedDataProvider.h>
#include <test/fuzz/fuzz_code/fuzz_node_setup.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

// ProcessMessages() handles one queued message per call; a few extra
// rounds let it drain anything the first message queued (e.g. getdata
// work), without letting one input loop forever.
static constexpr uint8_t MAX_PROCESS_ROUNDS = 8;

// One handshaken inbound-style peer for the lifetime of a single fuzz
// input: registered with PeerLogicValidation on construction (so
// State(nodeid) exists) and unregistered on destruction, the same set-up
// src/test/denialofservice_tests.cpp uses. INVALID_SOCKET means anything
// the node tries to send just fails and marks it for disconnection.
class FuzzPeer
{
public:
    explicit FuzzPeer(PeerLogicValidation& peer_logic)
        : m_peer_logic(peer_logic),
          m_node(NextNodeId(), ServiceFlags(NODE_NETWORK), 0, INVALID_SOCKET,
                 CAddress(CService(), NODE_NONE), 0, 0, CAddress(), "", /*fInboundIn=*/true)
    {
        m_node.SetSendVersion(PROTOCOL_VERSION);
        m_peer_logic.InitializeNode(&m_node);
        m_node.nVersion = PROTOCOL_VERSION;
        m_node.fSuccessfullyConnected = true;
    }

    ~FuzzPeer()
    {
        bool update_connection_time = false;
        m_peer_logic.FinalizeNode(m_node.GetId(), update_connection_time);
    }

    FuzzPeer(const FuzzPeer&) = delete;
    FuzzPeer& operator=(const FuzzPeer&) = delete;

    // Parses one message through CNetMessage's own readHeader()/readData()
    // -- the same code CNode::ReceiveMsgBytes() uses -- and queues it for
    // ProcessMessages(). Returns false if the header is rejected.
    bool Deliver(const std::string& command, const std::vector<uint8_t>& payload)
    {
        const CMessageHeader::MessageStartChars& message_start = FederationParams().MessageStart();

        CMessageHeader header(message_start, command.c_str(), payload.size());
        const uint256 checksum = Hash(payload.begin(), payload.end());
        std::memcpy(header.pchChecksum, checksum.begin(), CMessageHeader::CHECKSUM_SIZE);

        CDataStream header_stream(SER_NETWORK, INIT_PROTO_VERSION);
        header_stream << header;

        CNetMessage message(message_start, SER_NETWORK, INIT_PROTO_VERSION);
        if (message.readHeader(header_stream.data(), header_stream.size()) < 0) return false;
        size_t offset = 0;
        while (!message.complete()) {
            const int consumed = message.readData(reinterpret_cast<const char*>(payload.data()) + offset,
                                                  payload.size() - offset);
            if (consumed <= 0) return false;
            offset += consumed;
        }
        message.nTime = GetTimeMicros();

        LOCK(m_node.cs_vProcessMsg);
        m_node.nProcessQueueSize += message.vRecv.size() + CMessageHeader::HEADER_SIZE;
        m_node.vProcessMsg.push_back(std::move(message));
        return true;
    }

    void Process()
    {
        std::atomic<bool> interrupt{false};
        bool more_work = true;
        for (uint8_t round = 0; more_work && round < MAX_PROCESS_ROUNDS; ++round) {
            more_work = m_peer_logic.ProcessMessages(&m_node, interrupt);
        }
        // CConnman's message handler holds cs_sendProcessing around this.
        LOCK(m_node.cs_sendProcessing);
        m_peer_logic.SendMessages(&m_node);
    }

private:
    static NodeId NextNodeId()
    {
        static NodeId next_node_id{0};
        return next_node_id++;
    }

    PeerLogicValidation& m_peer_logic;
    CNode m_node;
};

static std::string ConsumeCommand(FuzzedDataProvider& fdp)
{
    const std::vector<std::string>& known_commands = getAllNetMessageTypes();
    if (fdp.ConsumeBool() || known_commands.empty()) {
        return fdp.ConsumeRandomLengthString(CMessageHeader::COMMAND_SIZE);
    }
    return known_commands[fdp.ConsumeIntegralInRange<size_t>(0, known_commands.size() - 1)];
}

static int test_one_input_processmessage(const uint8_t* data, size_t size)
{
    FuzzNodeSetup& setup = GetFuzzNodeSetup();

    FuzzedDataProvider fdp(data, size);
    const std::string command = ConsumeCommand(fdp);
    const std::vector<uint8_t> payload = fdp.ConsumeRemainingBytes<uint8_t>();

    FuzzPeer peer(*setup.peerLogic);
    if (!peer.Deliver(command, payload)) return 0;
    peer.Process();
    return 0;
}

// Thin forwarder kept under the shared generic name for pstt_fuzz_driver.h's
// own main() (AFL/stdin path) to call -- see that header's comment for why
// LLVMFuzzerTestOneInput below must call the uniquely-named function
// directly instead of through this wrapper.
static int test_one_input(const uint8_t* data, size_t size)
{
    return test_one_input_processmessage(data, size);
}

// This function is used by libFuzzer. Defined literally in this file
// (not in the shared pstt_fuzz_driver.h) so Fuzz Introspector's
// non-preprocessing, tree-sitter-based source analysis can correctly
// bind this call to this file's own test_one_input -- see
// pstt_fuzz_driver.h's own comment for why.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input_processmessage(data, size);
    return 0;
}

#include <test/fuzz/fuzz_code/pstt_fuzz_driver.h>
