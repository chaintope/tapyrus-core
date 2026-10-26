// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LOGGING_H
#define BITCOIN_LOGGING_H

#include <fs.h>
#include <tinyformat.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

static const bool DEFAULT_LOGTIMEMICROS = false;
static const bool DEFAULT_LOGIPS        = false;
static const bool DEFAULT_LOGTIMESTAMPS = true;
static const bool DEFAULT_LOGTHREADNAMES = false;
static const bool DEFAULT_LOGSOURCELOCATIONS = false;
static const bool DEFAULT_LOGLEVELALWAYS = false;
extern const char * const DEFAULT_DEBUGLOGFILE;

extern bool fLogIPs;

struct CLogCategoryActive
{
    std::string category;
    bool active;
};

/**
 * The file and line of a logging call site, with the function name taken from __func__.
 * Uses __builtin_FILE()/__builtin_LINE() rather than std::source_location::current():
 * Apple clang 15 evaluates the latter's default argument at the declaration, not the call site.
 */
class SourceLocation
{
public:
    /** func must be the __func__ of the logging call site. */
    SourceLocation(const char* func, const char* file = __builtin_FILE(), std::uint_least32_t line = __builtin_LINE())
        : m_func{func}, m_file{file}, m_line{line} {}

    std::string_view file_name() const { return m_file; }
    std::uint_least32_t line() const { return m_line; }
    std::string_view function_name_short() const { return m_func; }

private:
    std::string_view m_func;
    std::string_view m_file;
    std::uint_least32_t m_line;
};

struct SourceLocationEqual {
    bool operator()(const SourceLocation& lhs, const SourceLocation& rhs) const noexcept
    {
        return lhs.line() == rhs.line() && lhs.file_name() == rhs.file_name();
    }
};

struct SourceLocationHasher {
    size_t operator()(const SourceLocation& s) const noexcept
    {
        return std::hash<std::string_view>{}(s.file_name()) ^ (size_t{s.line()} << 1);
    }
};

namespace BCLog {
    enum LogFlags : uint32_t {
        NONE        = 0,
        NET         = (1 <<  0),
        TOR         = (1 <<  1),
        MEMPOOL     = (1 <<  2),
        HTTP        = (1 <<  3),
        BENCH       = (1 <<  4),
        ZMQ         = (1 <<  5),
        DB          = (1 <<  6),
        RPC         = (1 <<  7),
        ESTIMATEFEE = (1 <<  8),
        ADDRMAN     = (1 <<  9),
        SELECTCOINS = (1 << 10),
        REINDEX     = (1 << 11),
        CMPCTBLOCK  = (1 << 12),
        RAND        = (1 << 13),
        PRUNE       = (1 << 14),
        PROXY       = (1 << 15),
        MEMPOOLREJ  = (1 << 16),
        LIBEVENT    = (1 << 17),
        COINDB      = (1 << 18),
        QT          = (1 << 19),
        LEVELDB     = (1 << 20),
        ALL         = ~(uint32_t)0,
    };

    enum class Level {
        Trace = 0, // High-volume or detailed logging for development/debugging
        Debug,     // Reasonably noisy logging, but still usable in production
        Info,      // Default
        Warning,
        Error,
    };

    constexpr Level DEFAULT_LOG_LEVEL{Level::Debug};
    constexpr size_t DEFAULT_MAX_LOG_BUFFER{1'000'000}; // log data buffered before debug.log opens
    constexpr uint64_t RATELIMIT_MAX_BYTES{1024 * 1024}; // per source location, per RATELIMIT_WINDOW
    constexpr std::chrono::seconds RATELIMIT_WINDOW{3600};
    constexpr bool DEFAULT_LOGRATELIMIT{true};

    /** Escape control characters other than newline, so a log line cannot be forged or garbled. */
    std::string LogEscapeMessage(std::string_view str);

    //! Fixed window rate limiter for logging.
    class LogRateLimiter
    {
    public:
        //! Keeps track of an individual source location and how many available bytes are left for logging from it.
        struct Stats {
            //! Remaining bytes
            uint64_t m_available_bytes;
            //! Number of bytes that were consumed but didn't fit in the available bytes.
            uint64_t m_dropped_bytes{0};

            Stats(uint64_t max_bytes) : m_available_bytes{max_bytes} {}
            //! Updates internal accounting and returns true if enough available_bytes were remaining
            bool Consume(uint64_t bytes);
        };

    private:
        mutable std::mutex m_mutex;

        //! Stats for each source location that has attempted to log something. Guarded by m_mutex.
        std::unordered_map<SourceLocation, Stats, SourceLocationHasher, SourceLocationEqual> m_source_locations;
        //! Whether any log locations are suppressed. Cached view on m_source_locations for performance reasons.
        std::atomic<bool> m_suppression_active{false};
        LogRateLimiter(uint64_t max_bytes, std::chrono::seconds reset_window);

    public:
        using SchedulerFunction = std::function<void(std::function<void()>, std::chrono::milliseconds)>;
        /**
         * @param scheduler_func    Schedules the window reset: called once with the reset
         *                          function and the reset_window interval.
         * @param max_bytes         Maximum number of bytes that can be logged for each source location.
         * @param reset_window      Time window after which the stats are reset.
         */
        static std::shared_ptr<LogRateLimiter> Create(
            SchedulerFunction&& scheduler_func,
            uint64_t max_bytes,
            std::chrono::seconds reset_window);
        //! Maximum number of bytes logged per location per window.
        const uint64_t m_max_bytes;
        //! Interval after which the window is reset.
        const std::chrono::seconds m_reset_window;
        //! Suppression status of a source log location.
        enum class Status {
            UNSUPPRESSED,     // string fits within the limit
            NEWLY_SUPPRESSED, // suppression has started since this string
            STILL_SUPPRESSED, // suppression is still ongoing
        };
        //! Consumes `source_loc`'s available bytes for the (formatted) `str` and returns its status.
        [[nodiscard]] Status Consume(const SourceLocation& source_loc, const std::string& str);
        //! Resets all usage to zero. Called periodically by the scheduler.
        void Reset();
        //! Returns true if any log locations are currently being suppressed.
        bool SuppressionsActive() const { return m_suppression_active; }
    };

    class Logger
    {
    private:
        //! Serializes output, and guards m_category_log_levels and m_limiter.
        mutable std::mutex m_cs;

        /**
         * m_started_new_line is a state variable that will suppress printing of
         * the timestamp when multiple calls are made that don't end in a
         * newline.
         */
        std::atomic_bool m_started_new_line{true};

        /** Log categories bitfield. */
        std::atomic<uint32_t> m_categories{0};

        //! Below this level, messages of categories without their own level are dropped.
        std::atomic<Level> m_log_level{DEFAULT_LOG_LEVEL};

        //! Category-specific log level. Overrides m_log_level.
        std::unordered_map<LogFlags, Level> m_category_log_levels;

        //! Rate limits each log location; null when rate limiting is off.
        std::shared_ptr<LogRateLimiter> m_limiter;

        std::string LogTimestampStr() const;
        std::string GetLogPrefix(LogFlags category, Level level) const;
        std::string LinePrefix(const SourceLocation& source_loc) const;
        void Output(const std::string& line, bool to_file);
        void LogPrintStr_(std::string_view str, const SourceLocation& source_loc, LogFlags category, Level level, bool should_ratelimit);

    protected:
        /** Write a timestamped line to an output other than the console. */
        virtual void WriteLine(const std::string& line) {}

    public:
        bool m_print_to_console = false;

        bool m_log_timestamps = DEFAULT_LOGTIMESTAMPS;
        bool m_log_time_micros = DEFAULT_LOGTIMEMICROS;
        bool m_log_threadnames = DEFAULT_LOGTHREADNAMES;
        bool m_log_sourcelocations = DEFAULT_LOGSOURCELOCATIONS;
        bool m_always_print_category_level = DEFAULT_LOGLEVELALWAYS;

        /** Send a message to the log output, as one line with category and level prefixes. */
        void LogPrintStr(std::string_view str, const SourceLocation& source_loc, LogFlags category, Level level, bool should_ratelimit);

        /**
         * Send a LogPrintf/LogPrint message to the log output: no category or level
         * prefix, no added newline and no rate limiting, as before the levels existed.
         */
        void LogPrintLegacyStr(const std::string& str, const SourceLocation& source_loc);

        /** Returns whether logs will be written to any output */
        virtual bool Enabled() const { return m_print_to_console; }

        void SetRateLimiting(std::shared_ptr<LogRateLimiter> limiter);

        Level LogLevel() const { return m_log_level.load(); }
        void SetLogLevel(Level level) { m_log_level = level; }
        bool SetLogLevel(std::string_view level_str);
        bool SetCategoryLogLevel(const std::string& category_str, std::string_view level_str);

        uint32_t GetCategoryMask() const { return m_categories.load(); }

        void EnableCategory(LogFlags flag);
        bool EnableCategory(const std::string& str);
        void DisableCategory(LogFlags flag);
        bool DisableCategory(const std::string& str);

        bool WillLogCategory(LogFlags category) const;
        bool WillLogCategoryLevel(LogFlags category, Level level) const;

        //! Returns a string with all user-selectable log levels.
        static std::string LogLevelsString();

        //! Returns the string representation of a log level.
        static std::string LogLevelToStr(Level level);
    };

} // namespace BCLog

BCLog::Logger& LogInstance();

/** Replace the logger LogInstance() returns. Call before other threads start. */
void SetLogInstance(BCLog::Logger* logger);

/** Return true if log accepts specified category */
static inline bool LogAcceptCategory(BCLog::LogFlags category)
{
    return LogInstance().WillLogCategory(category);
}

/** Return true if log accepts specified category, at the specified level */
static inline bool LogAcceptCategory(BCLog::LogFlags category, BCLog::Level level)
{
    return LogInstance().WillLogCategoryLevel(category, level);
}

/** Returns a string with the log categories. */
std::string ListLogCategories();

/** Returns a vector of the active log categories. */
std::vector<CLogCategoryActive> ListActiveLogCategories();

/** Return true if str parses as a log category and set the flag */
bool GetLogCategory(BCLog::LogFlags& flag, const std::string& str);

/** Get format string from VA_ARGS for error reporting */
template<typename... Args> std::string FormatStringFromLogArgs(const char *fmt, const Args&... args) { return fmt; }

template <typename... Args>
inline void LogPrintFormatInternal(const SourceLocation& source_loc, BCLog::LogFlags flag, BCLog::Level level, bool should_ratelimit, const char* fmt, const Args&... args)
{
    if (!LogInstance().Enabled()) return;
    std::string log_msg;
    try {
        log_msg = tfm::format(fmt, args...);
    } catch (tinyformat::format_error& fmterr) {
        log_msg = "Error \"" + std::string{fmterr.what()} + "\" while formatting log message: " + fmt;
    }
    LogInstance().LogPrintStr(log_msg, source_loc, flag, level, should_ratelimit);
}

static inline void MarkUsed() {}
template<typename T, typename... Args> static inline void MarkUsed(const T& t, const Args&... args)
{
    (void)t;
    MarkUsed(args...);
}

// Be conservative when using LogPrintf/LogInfo/error or other things which
// unconditionally log to debug.log! It should not be the case that an inbound
// peer can fill up a user's disk with debug.log entries.

#ifdef USE_COVERAGE
#define LogPrintf(...) do { MarkUsed(__VA_ARGS__); } while(0)
#define LogPrint(category, ...) do { MarkUsed(__VA_ARGS__); } while(0)
#define LogInfo(...) do { MarkUsed(__VA_ARGS__); } while(0)
#define LogWarning(...) do { MarkUsed(__VA_ARGS__); } while(0)
#define LogError(...) do { MarkUsed(__VA_ARGS__); } while(0)
#define LogDebug(category, ...) do { MarkUsed(__VA_ARGS__); } while(0)
#define LogTrace(category, ...) do { MarkUsed(__VA_ARGS__); } while(0)
#else
#define LogPrintLevel_(category, level, should_ratelimit, ...) LogPrintFormatInternal(SourceLocation{__func__}, category, level, should_ratelimit, __VA_ARGS__)

// Log unconditionally. Rate limited per source location to mitigate disk filling attacks.
#define LogInfo(...) LogPrintLevel_(BCLog::LogFlags::ALL, BCLog::Level::Info, /*should_ratelimit=*/true, __VA_ARGS__)
#define LogWarning(...) LogPrintLevel_(BCLog::LogFlags::ALL, BCLog::Level::Warning, /*should_ratelimit=*/true, __VA_ARGS__)
#define LogError(...) LogPrintLevel_(BCLog::LogFlags::ALL, BCLog::Level::Error, /*should_ratelimit=*/true, __VA_ARGS__)

// Log only if the category is enabled at this level, prefixed with the category.
// No rate limiting: -debug users are expected to accept the disk usage.
// A macro, so the arguments are not evaluated when the category is disabled.
#define LogDebug(category, ...) do { \
    if (LogAcceptCategory((category), BCLog::Level::Debug)) { \
        LogPrintLevel_((category), BCLog::Level::Debug, /*should_ratelimit=*/false, __VA_ARGS__); \
    } \
} while(0)
#define LogTrace(category, ...) do { \
    if (LogAcceptCategory((category), BCLog::Level::Trace)) { \
        LogPrintLevel_((category), BCLog::Level::Trace, /*should_ratelimit=*/false, __VA_ARGS__); \
    } \
} while(0)

// Old interface, kept until its callers move to the macros above.
#define LogPrintf(...) do { \
    if (LogInstance().Enabled()) { \
        std::string _log_msg_; /* Unlikely name to avoid shadowing variables */ \
        try { \
            _log_msg_ = tfm::format(__VA_ARGS__); \
        } catch (tinyformat::format_error &fmterr) { \
            /* Original format string will have newline so don't add one here */ \
            _log_msg_ = "Error \"" + std::string(fmterr.what()) + "\" while formatting log message: " + FormatStringFromLogArgs(__VA_ARGS__); \
        } \
        LogInstance().LogPrintLegacyStr(_log_msg_, SourceLocation{__func__}); \
    } \
} while(0)

#define LogPrint(category, ...) do { \
    if (LogAcceptCategory((category), BCLog::Level::Debug)) { \
        LogPrintf(__VA_ARGS__); \
    } \
} while(0)
#endif

#endif // BITCOIN_LOGGING_H
