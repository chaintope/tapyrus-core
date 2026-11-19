// Copyright (c) 2024-present The Bitcoin Core developers
 // Distributed under the MIT software license, see the accompanying
 // file COPYING or http://www.opensource.org/licenses/mit-license.php.

 #include <logging.h>
 #include <timeoffsets.h>
 #include <sync.h>
 #include <utiltime.h>
 #include <notifications.h>

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
         m_notifications.ReportClockInSync();
         return false;
     }

     m_notifications.ReportClockOutOfSync(WARN_THRESHOLD);
     return true;
 }