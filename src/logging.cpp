// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <logging.h>
#include <threadnames.h>
#include <utiltime.h>

#include <cassert>
#include <optional>

const char * const DEFAULT_DEBUGLOGFILE = "debug.log";

static std::atomic<BCLog::Logger*>& LogInstancePtr()
{
/**
 * NOTE: the logger instances is leaked on exit. This is ugly, but will be
 * cleaned up by the OS/libc. Defining a logger as a global object doesn't work
 * since the order of destruction of static/global objects is undefined.
 * Consider if the logger gets destroyed, and then some later destructor calls
 * LogPrintf, maybe indirectly, and you get a core dump at shutdown trying to
 * access the logger. When the shutdown sequence is fully audited and tested,
 * explicit destruction of these objects can be implemented by changing this
 * from a raw pointer to a std::unique_ptr.
 * Since the ~Logger() destructor is never called, the Logger class and all
 * its subclasses must have implicitly-defined destructors.
 *
 * This method of initialization was originally introduced in
 * ee3374234c60aba2cc4c5cd5cac1c0aefc2d817c.
 */
    static std::atomic<BCLog::Logger*> g_logger{new BCLog::Logger()};
    return g_logger;
}

BCLog::Logger& LogInstance()
{
    return *LogInstancePtr().load();
}

// The replaced instance is leaked like the first one, so a thread still
// logging through it is safe.
void SetLogInstance(BCLog::Logger* logger)
{
    LogInstancePtr().store(logger);
}

bool fLogIPs = DEFAULT_LOGIPS;

void BCLog::Logger::EnableCategory(BCLog::LogFlags flag)
{
    m_categories |= flag;
}

bool BCLog::Logger::EnableCategory(const std::string& str)
{
    BCLog::LogFlags flag;
    if (!GetLogCategory(flag, str)) return false;
    EnableCategory(flag);
    return true;
}

void BCLog::Logger::DisableCategory(BCLog::LogFlags flag)
{
    m_categories &= ~flag;
}

bool BCLog::Logger::DisableCategory(const std::string& str)
{
    BCLog::LogFlags flag;
    if (!GetLogCategory(flag, str)) return false;
    DisableCategory(flag);
    return true;
}

bool BCLog::Logger::WillLogCategory(BCLog::LogFlags category) const
{
    return (m_categories.load(std::memory_order_relaxed) & category) != 0;
}

bool BCLog::Logger::WillLogCategoryLevel(BCLog::LogFlags category, BCLog::Level level) const
{
    // Log messages at Info, Warning and Error level unconditionally, so that
    // important troubleshooting information doesn't get lost.
    if (level >= BCLog::Level::Info) return true;

    if (!WillLogCategory(category)) return false;

    std::lock_guard<std::mutex> lock(m_cs);
    const auto it{m_category_log_levels.find(category)};
    return level >= (it == m_category_log_levels.end() ? LogLevel() : it->second);
}

struct CLogCategoryDesc
{
    BCLog::LogFlags flag;
    std::string category;
};

const CLogCategoryDesc LogCategories[] =
{
    {BCLog::NONE, "0"},
    {BCLog::NONE, "none"},
    {BCLog::NET, "net"},
    {BCLog::TOR, "tor"},
    {BCLog::MEMPOOL, "mempool"},
    {BCLog::HTTP, "http"},
    {BCLog::BENCH, "bench"},
    {BCLog::ZMQ, "zmq"},
    {BCLog::DB, "db"},
    {BCLog::RPC, "rpc"},
    {BCLog::ESTIMATEFEE, "estimatefee"},
    {BCLog::ADDRMAN, "addrman"},
    {BCLog::SELECTCOINS, "selectcoins"},
    {BCLog::REINDEX, "reindex"},
    {BCLog::CMPCTBLOCK, "cmpctblock"},
    {BCLog::RAND, "rand"},
    {BCLog::PRUNE, "prune"},
    {BCLog::PROXY, "proxy"},
    {BCLog::MEMPOOLREJ, "mempoolrej"},
    {BCLog::LIBEVENT, "libevent"},
    {BCLog::COINDB, "coindb"},
    {BCLog::QT, "qt"},
    {BCLog::LEVELDB, "leveldb"},
    {BCLog::ALL, "1"},
    {BCLog::ALL, "all"},
};

bool GetLogCategory(BCLog::LogFlags& flag, const std::string& str)
{
    if (str == "") {
        flag = BCLog::ALL;
        return true;
    }
    for (const CLogCategoryDesc& category_desc : LogCategories) {
        if (category_desc.category == str) {
            flag = category_desc.flag;
            return true;
        }
    }
    return false;
}

std::string ListLogCategories()
{
    std::string ret;
    int outcount = 0;
    for (const CLogCategoryDesc& category_desc : LogCategories) {
        // Omit the special cases.
        if (category_desc.flag != BCLog::NONE && category_desc.flag != BCLog::ALL) {
            if (outcount != 0) ret += ", ";
            ret += category_desc.category;
            outcount++;
        }
    }
    return ret;
}

std::vector<CLogCategoryActive> ListActiveLogCategories()
{
    std::vector<CLogCategoryActive> ret;
    for (const CLogCategoryDesc& category_desc : LogCategories) {
        // Omit the special cases.
        if (category_desc.flag != BCLog::NONE && category_desc.flag != BCLog::ALL) {
            CLogCategoryActive catActive;
            catActive.category = category_desc.category;
            catActive.active = LogAcceptCategory(category_desc.flag);
            ret.push_back(catActive);
        }
    }
    return ret;
}

static std::string LogCategoryToStr(BCLog::LogFlags category)
{
    if (category == BCLog::ALL) return "all";
    for (const CLogCategoryDesc& category_desc : LogCategories) {
        if (category_desc.flag == category) return category_desc.category;
    }
    return "unknown";
}

static std::optional<BCLog::Level> GetLogLevel(std::string_view level_str)
{
    if (level_str == "trace") return BCLog::Level::Trace;
    if (level_str == "debug") return BCLog::Level::Debug;
    if (level_str == "info") return BCLog::Level::Info;
    if (level_str == "warning") return BCLog::Level::Warning;
    if (level_str == "error") return BCLog::Level::Error;
    return std::nullopt;
}

// Info and above are always logged, so the user only chooses among these.
static constexpr BCLog::Level MAX_USER_SETABLE_SEVERITY_LEVEL{BCLog::Level::Info};

std::string BCLog::Logger::LogLevelToStr(BCLog::Level level)
{
    switch (level) {
    case BCLog::Level::Trace:
        return "trace";
    case BCLog::Level::Debug:
        return "debug";
    case BCLog::Level::Info:
        return "info";
    case BCLog::Level::Warning:
        return "warning";
    case BCLog::Level::Error:
        return "error";
    }
    assert(false);
}

std::string BCLog::Logger::LogLevelsString()
{
    return LogLevelToStr(Level::Info) + ", " + LogLevelToStr(Level::Debug) + ", " + LogLevelToStr(Level::Trace);
}

bool BCLog::Logger::SetLogLevel(std::string_view level_str)
{
    const auto level{GetLogLevel(level_str)};
    if (!level || *level > MAX_USER_SETABLE_SEVERITY_LEVEL) return false;
    m_log_level = *level;
    return true;
}

bool BCLog::Logger::SetCategoryLogLevel(const std::string& category_str, std::string_view level_str)
{
    BCLog::LogFlags flag;
    if (!GetLogCategory(flag, category_str)) return false;

    const auto level{GetLogLevel(level_str)};
    if (!level || *level > MAX_USER_SETABLE_SEVERITY_LEVEL) return false;

    std::lock_guard<std::mutex> lock(m_cs);
    m_category_log_levels[flag] = *level;
    return true;
}

std::string BCLog::Logger::LogTimestampStr() const
{
    if (!m_log_timestamps) return {};

    int64_t nTimeMicros = GetTimeMicros();
    std::string strStamped = FormatISO8601DateTime(nTimeMicros/1000000);
    if (m_log_time_micros) {
        strStamped.pop_back();
        strStamped += strprintf(".%06dZ", nTimeMicros%1000000);
    }
    int64_t mocktime = GetMockTime();
    if (mocktime) {
        strStamped += " (mocktime: " + FormatISO8601DateTime(mocktime) + ")";
    }
    strStamped += ' ';
    return strStamped;
}

std::string BCLog::LogEscapeMessage(std::string_view str)
{
    std::string ret;
    for (char ch_in : str) {
        uint8_t ch = (uint8_t)ch_in;
        if ((ch >= 32 || ch == '\n') && ch != '\x7f') {
            ret += ch_in;
        } else {
            ret += strprintf("\\x%02x", ch);
        }
    }
    return ret;
}

std::string BCLog::Logger::GetLogPrefix(BCLog::LogFlags category, BCLog::Level level) const
{
    if (category == LogFlags::NONE) category = LogFlags::ALL;

    const bool has_category{m_always_print_category_level || category != LogFlags::ALL};

    // If there is no category, Info is implied
    if (!has_category && level == Level::Info) return {};

    std::string s{"["};
    if (has_category) {
        s += LogCategoryToStr(category);
    }

    // If there is a category, Debug is implied, so don't add the level
    if (m_always_print_category_level || !has_category || level != Level::Debug) {
        // Only add separator if we have a category
        if (has_category) s += ":";
        s += LogLevelToStr(level);
    }

    s += "] ";
    return s;
}

std::string BCLog::Logger::LinePrefix(const SourceLocation& source_loc) const
{
    std::string prefix;
    if (m_log_threadnames) {
        const std::string threadname{ThreadGetInternalName()};
        prefix += strprintf("[%s] ", threadname.empty() ? "unknown" : threadname);
    }
    if (m_log_sourcelocations) {
        std::string_view file{source_loc.file_name()};
        if (file.starts_with("./")) file.remove_prefix(2);
        prefix += strprintf("[%s:%d] [%s] ", std::string{file}, source_loc.line(), std::string{source_loc.function_name_short()});
    }
    return prefix;
}

void BCLog::Logger::Output(const std::string& line)
{
    if (m_print_to_console) {
        // print to console
        fwrite(line.data(), 1, line.size(), stdout);
        fflush(stdout);
    }
    WriteLine(line);
}

void BCLog::Logger::LogPrintStr(std::string_view str, const SourceLocation& source_loc, BCLog::LogFlags category, BCLog::Level level)
{
    std::lock_guard<std::mutex> lock(m_cs);

    std::string line = LogEscapeMessage(str);
    if (!line.ends_with('\n')) line.push_back('\n');
    line.insert(0, GetLogPrefix(category, level));
    line.insert(0, LinePrefix(source_loc));
    line.insert(0, LogTimestampStr());

    Output(line);
    m_started_new_line = true;
}

void BCLog::Logger::LogPrintLegacyStr(const std::string& str, const SourceLocation& source_loc)
{
    std::lock_guard<std::mutex> lock(m_cs);

    std::string line = LogEscapeMessage(str);
    if (m_started_new_line) {
        line.insert(0, GetLogPrefix(LogFlags::ALL, Level::Info));
        line.insert(0, LinePrefix(source_loc));
        line.insert(0, LogTimestampStr());
    }
    m_started_new_line = !str.empty() && str.back() == '\n';

    Output(line);
}
