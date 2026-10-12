// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <debuglogfile.h>

#include <cassert>
#include <vector>

static int FileWriteStr(const std::string &str, FILE *fp)
{
    return fwrite(str.data(), 1, str.size(), fp);
}

// Approximate memory a buffered line takes: its list node and the string's heap buffer.
static size_t MemUsage(const std::string& line)
{
    return sizeof(std::string) + 2 * sizeof(void*) + line.capacity();
}

bool DebugLogFile::Open()
{
    size_t lines_discarded;
    {
        std::lock_guard<std::mutex> scoped_lock(m_file_mutex);

        assert(m_fileout == nullptr);
        assert(!m_file_path.empty());

        const char* mode = fs::exists(m_file_path) && fs::is_regular_file(m_file_path) ? "a" : "w";
        m_fileout = fsbridge::fopen(m_file_path, mode);
        if (!m_fileout) {
            return false;
        }

        setbuf(m_fileout, nullptr); // unbuffered
        // dump buffered messages from before we opened the log
        while (!m_msgs_before_open.empty()) {
            FileWriteStr(m_msgs_before_open.front(), m_fileout);
            m_msgs_before_open.pop_front();
        }
        m_cur_buffer_memusage = 0;
        lines_discarded = m_buffer_lines_discarded;
    }

    if (lines_discarded > 0) {
        LogPrintStr(strprintf("Early logging buffer overflowed, %d log lines discarded.\n", lines_discarded),
                    SourceLocation{__func__}, BCLog::ALL, BCLog::Level::Info);
    }
    return true;
}

void DebugLogFile::WriteLine(const std::string& line)
{
    std::lock_guard<std::mutex> scoped_lock(m_file_mutex);

    // buffer if we haven't opened the log yet, dropping the oldest lines past the limit
    if (m_fileout == nullptr) {
        m_msgs_before_open.push_back(line);
        m_cur_buffer_memusage += MemUsage(m_msgs_before_open.back());
        while (m_cur_buffer_memusage > m_max_buffer_memusage && !m_msgs_before_open.empty()) {
            m_cur_buffer_memusage -= MemUsage(m_msgs_before_open.front());
            m_msgs_before_open.pop_front();
            ++m_buffer_lines_discarded;
        }
        return;
    }

    // reopen the log file, if requested
    if (m_reopen_file) {
        m_reopen_file = false;
        FILE* new_fileout = fsbridge::fopen(m_file_path, "a");
        if (new_fileout) {
            setbuf(new_fileout, nullptr); // unbuffered
            fclose(m_fileout);
            m_fileout = new_fileout;
        }
    }
    FileWriteStr(line, m_fileout);
}

void DebugLogFile::Shrink()
{
    // Amount of debug.log to save at end when shrinking (must fit in memory)
    constexpr size_t RECENT_DEBUG_HISTORY_SIZE = 10 * 1000000;

    assert(!m_file_path.empty());

    // Scroll debug.log if it's getting too big
    FILE* file = fsbridge::fopen(m_file_path, "r");

    // Special files (e.g. device nodes) may not have a size.
    size_t log_size = 0;
    try {
        log_size = fs::file_size(m_file_path);
    } catch (fs::filesystem_error &) {}

    // If debug.log file is more than 10% bigger the RECENT_DEBUG_HISTORY_SIZE
    // trim it down by saving only the last RECENT_DEBUG_HISTORY_SIZE bytes
    if (file && log_size > 11 * (RECENT_DEBUG_HISTORY_SIZE / 10))
    {
        // Restart the file with some of the end
        std::vector<char> vch(RECENT_DEBUG_HISTORY_SIZE, 0);
        if (fseek(file, -((long)vch.size()), SEEK_END)) {
            LogPrintf("Failed to shrink debug log file: fseek(...) failed\n");
            fclose(file);
            return;
        }
        int nBytes = fread(vch.data(), 1, vch.size(), file);
        fclose(file);

        file = fsbridge::fopen(m_file_path, "w");
        if (file)
        {
            fwrite(vch.data(), 1, nBytes, file);
            fclose(file);
        }
    }
    else if (file != nullptr)
        fclose(file);
}
