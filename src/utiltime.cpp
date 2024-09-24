// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <utiltime.h>
#include <timeoffsets.h>

#include <atomic>
#include <ctime>
#include <thread>
#include <iomanip>
#include <tinyformat.h>

static std::atomic<std::chrono::seconds> nMockTime{}; //!< For testing

void SetMockTime(int64_t nMockTimeIn)
{
    nMockTime.store(std::chrono::seconds(nMockTimeIn), std::memory_order_relaxed);
}

int64_t GetMockTime()
{
    return nMockTime.load(std::memory_order_relaxed).count();
}

static std::chrono::time_point<std::chrono::system_clock> GetSystemTime()
{
    const auto mocktime{nMockTime.load(std::memory_order_relaxed)};
    const auto now = (mocktime.count() != 0) ?
         std::chrono::time_point<std::chrono::system_clock>(mocktime) :
         std::chrono::system_clock::now();
    return now;
}

int64_t GetTimeMicros()
{
    int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(GetSystemTime().time_since_epoch()).count();
    assert(now > 0);
    return now;
}

int64_t GetTimeMillis()
{
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(GetSystemTime().time_since_epoch()).count();
    assert(now > 0);
    return now;
}

int64_t GetSystemTimeInSeconds()
{
    return GetTimeMicros()/1000000;
}

int64_t GetTime()
{
    return GetSystemTimeInSeconds();
}

int64_t GetAdjustedTime()
{
    return GetSystemTimeInSeconds();
}


void MilliSleep(int64_t n)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(n));
}

// Convert time_point to time_t
std::tm fromTimePoint(int64_t nTime) {
    std::chrono::time_point<std::chrono::system_clock> tp =
        std::chrono::system_clock::time_point(std::chrono::seconds(nTime));
    std::time_t timeT = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = *std::gmtime(&timeT); // Get UTC time
    return tm;
}

std::string FormatISO8601DateTime(int64_t nTime) {
    std::tm tm = fromTimePoint(nTime);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string FormatISO8601Date(int64_t nTime) {
    std::tm tm = fromTimePoint(nTime);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d");
    return oss.str();
}

std::string FormatISO8601Time(int64_t nTime) {
    std::tm tm = fromTimePoint(nTime);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%SZ");
    return oss.str();
}
