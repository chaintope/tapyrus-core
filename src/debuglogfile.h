// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_DEBUGLOGFILE_H
#define TAPYRUS_DEBUGLOGFILE_H

#include <fs.h>
#include <logging.h>

#include <atomic>
#include <cstdio>
#include <list>
#include <mutex>
#include <string>

/**
 * The logger tapyrusd and tapyrus-qt install at start-up: the base logger
 * plus debug.log. Lines logged before Open() are buffered, up to a memory
 * limit, and written once the file is open.
 */
class DebugLogFile : public BCLog::Logger
{
private:
    FILE* m_fileout = nullptr;
    std::mutex m_file_mutex;
    std::list<std::string> m_msgs_before_open;
    const size_t m_max_buffer_memusage;
    size_t m_cur_buffer_memusage{0};
    size_t m_buffer_lines_discarded{0};
    std::atomic<bool> m_reopen_file{false};

protected:
    void WriteLine(const std::string& line) override;

public:
    const fs::path m_file_path;

    explicit DebugLogFile(const fs::path& file_path, size_t max_buffer_memusage = BCLog::DEFAULT_MAX_LOG_BUFFER)
        : m_max_buffer_memusage(max_buffer_memusage), m_file_path(file_path) {}

    bool Enabled() const override { return true; }

    /** Open debug.log and write the buffered lines; logs how many were dropped, if any. */
    bool Open();

    /** Trim debug.log to its most recent part if it has grown too large. */
    void Shrink();

    /** Reopen debug.log before the next write. Safe to call from a signal handler. */
    void Reopen() { m_reopen_file = true; }

    /** Shrink by default only when no debug categories are enabled. */
    bool DefaultShrink() const { return GetCategoryMask() == BCLog::NONE; }
};

#endif // TAPYRUS_DEBUGLOGFILE_H
