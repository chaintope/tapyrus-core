// Copyright (c) 2024-present The Bitcoin Core developers
 // Distributed under the MIT software license, see the accompanying
 // file COPYING or http://www.opensource.org/licenses/mit-license.php.

 #include <logging.h>
 #include <ui_interface.h>
 #include <timeoffsets.h>
 #include <sync.h>
 #include <tinyformat.h>
 #include <utiltime.h>
 #include <util.h>
 #include <warnings.h>

 #include <algorithm>
 #include <chrono>
 #include <cstdint>
 #include <deque>
 #include <limits>
 #include <optional>

using namespace std::chrono_literals;

 void TimeOffsets::Add(std::chrono::seconds offset)
 {
     LOCK(m_mutex);

     if (m_offsets.size() >= MAX_SIZE) {
         m_offsets.pop_front();
     }
     m_offsets.push_back(offset);
     LogPrint(BCLog::NET, "Added time offset %+ds, total samples %d\n",
              std::chrono::duration_cast<std::chrono::seconds>(offset).count(), m_offsets.size());
 }

 std::chrono::seconds TimeOffsets::Median() const
 {
     LOCK(m_mutex);

     // Only calculate the median if we have 5 or more offsets
     if (m_offsets.size() < 5) return 0s;

     auto sorted_copy = m_offsets;
     std::sort(sorted_copy.begin(), sorted_copy.end());
     return sorted_copy[sorted_copy.size() / 2];  // approximate median is good enough, keep it simple
 }

 bool TimeOffsets::WarnIfOutOfSync() const
 {
     // when median == std::numeric_limits<int64_t>::min(), calling std::chrono::abs is UB
     auto median{std::max(Median(), std::chrono::seconds(std::numeric_limits<int64_t>::min() + 1))};
     if (std::chrono::abs(median) <= WARN_THRESHOLD) {
         SetMedianTimeOffsetWarning("");
         uiInterface.NotifyAlertChanged();
         return false;
     }

     std::string msg{strprintf(_(
         "Your computer's date and time appear to be more than %d minutes out of sync with the network, "
         "this may lead to consensus failure. After you've confirmed your computer's clock, this message "
         "should no longer appear when you restart your node. Without a restart, it should stop showing "
         "automatically after you've connected to a sufficient number of new outbound peers, which may "
         "take some time. You can inspect the `timeoffset` field of the `getpeerinfo` and `getnetworkinfo` "
         "RPC methods to get more info."
     ), std::chrono::duration_cast<std::chrono::minutes>(WARN_THRESHOLD).count())};
     LogPrintf("TimeOffsets::WarnIfOutOfSync %s\n", msg.c_str());
     SetMedianTimeOffsetWarning(msg);
     uiInterface.NotifyAlertChanged();
     return true;
 }