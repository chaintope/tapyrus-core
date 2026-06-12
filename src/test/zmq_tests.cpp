// Copyright (c) 2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>
#include <tapyrus-config.h>

#if ENABLE_ZMQ

#include <zmq/zmqpublishnotifier.h>

BOOST_AUTO_TEST_SUITE(zmq_tests)

// Shutdown() must be a no-op when psocket is nullptr (notifier never initialized).
// Previously the assert(psocket) would fire → SIGABRT in Release builds.
BOOST_AUTO_TEST_CASE(shutdown_without_initialize_noop)
{
    CZMQPublishHashBlockNotifier notifier;
    BOOST_CHECK_NO_THROW(notifier.Shutdown());
    // Idempotent: second call also safe after psocket stays nullptr
    BOOST_CHECK_NO_THROW(notifier.Shutdown());
}

// SetType/SetAddress/GetType/GetAddress must round-trip correctly.
BOOST_AUTO_TEST_CASE(notifier_accessors)
{
    CZMQPublishHashBlockNotifier notifier;
    notifier.SetType("pubhashblock");
    notifier.SetAddress("tcp://127.0.0.1:28332");
    BOOST_CHECK_EQUAL(notifier.GetType(), "pubhashblock");
    BOOST_CHECK_EQUAL(notifier.GetAddress(), "tcp://127.0.0.1:28332");
}

// Initialize with a null context returns false and leaves psocket null,
// so a subsequent Shutdown() is still safe.
BOOST_AUTO_TEST_CASE(initialize_null_context_fails_gracefully)
{
    CZMQPublishHashBlockNotifier notifier;
    notifier.SetType("pubhashblock");
    notifier.SetAddress("inproc://tapyrus_zmq_test_null_ctx");
    BOOST_CHECK(!notifier.Initialize(nullptr));
    BOOST_CHECK_NO_THROW(notifier.Shutdown());
}

// Happy path: Initialize with a valid context and inproc address, then Shutdown.
BOOST_AUTO_TEST_CASE(initialize_and_shutdown)
{
    void* ctx = zmq_ctx_new();
    BOOST_REQUIRE(ctx != nullptr);

    CZMQPublishHashBlockNotifier notifier;
    notifier.SetType("pubhashblock");
    notifier.SetAddress("inproc://tapyrus_zmq_test_init");

    BOOST_CHECK(notifier.Initialize(ctx));
    BOOST_CHECK_NO_THROW(notifier.Shutdown());

    zmq_ctx_destroy(ctx);
}

// After Initialize + Shutdown, a second Shutdown must be a no-op
// (psocket is nullptr and the guard prevents double-close).
BOOST_AUTO_TEST_CASE(shutdown_after_initialize_idempotent)
{
    void* ctx = zmq_ctx_new();
    BOOST_REQUIRE(ctx != nullptr);

    CZMQPublishHashBlockNotifier notifier;
    notifier.SetType("pubhashblock");
    notifier.SetAddress("inproc://tapyrus_zmq_test_idem");

    BOOST_CHECK(notifier.Initialize(ctx));
    BOOST_CHECK_NO_THROW(notifier.Shutdown());
    BOOST_CHECK_NO_THROW(notifier.Shutdown());  // second call must not abort

    zmq_ctx_destroy(ctx);
}

// Two notifiers bound to the same address reuse the underlying socket.
// Both Initialize() calls succeed; both Shutdown() calls complete without error,
// and the socket is only closed when the last user shuts down.
BOOST_AUTO_TEST_CASE(shared_socket_reuse)
{
    void* ctx = zmq_ctx_new();
    BOOST_REQUIRE(ctx != nullptr);

    const std::string address = "inproc://tapyrus_zmq_test_shared";

    CZMQPublishHashBlockNotifier first;
    first.SetType("pubhashblock");
    first.SetAddress(address);

    CZMQPublishHashTransactionNotifier second;
    second.SetType("pubhashtx");
    second.SetAddress(address);

    BOOST_CHECK(first.Initialize(ctx));
    BOOST_CHECK(second.Initialize(ctx));   // reuses socket opened by first

    BOOST_CHECK_NO_THROW(first.Shutdown());   // ref-count drops to 1; socket kept open
    BOOST_CHECK_NO_THROW(second.Shutdown());  // ref-count drops to 0; socket closed

    zmq_ctx_destroy(ctx);
}

BOOST_AUTO_TEST_SUITE_END()

#endif // ENABLE_ZMQ
