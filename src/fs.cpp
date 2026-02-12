// Copyright (c) 2017-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <fs.h>

#ifndef WIN32
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/utsname.h>
#include <unistd.h>
#else
#include <limits>
#include <windows.h>
#include <tinyformat.h>
#endif

#include <cassert>
#include <cerrno>
#include <string>
#include <sstream>

#ifdef WIN32
/** Convert UTF-8 std::string to UTF-16 std::wstring using Windows API. */
static std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &result[0], len);
    return result;
}

/** Convert UTF-16 std::wstring to UTF-8 std::string using Windows API. */
static std::string WideToUtf8(const std::wstring& wide)
{
    if (wide.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), &result[0], len, nullptr, nullptr);
    return result;
}
#endif

namespace fsbridge {

FILE *fopen(const fs::path& p, const char *mode)
{
#ifndef WIN32
    return ::fopen(p.c_str(), mode);
#else
    std::wstring wmode = Utf8ToWide(mode);
    return ::_wfopen(p.wstring().c_str(), wmode.c_str());
#endif
}

fs::path AbsPathJoin(const fs::path& base, const fs::path& path)
{
    assert(base.is_absolute());
    return path.empty() ? base : fs::path(base / path);
}

#ifndef WIN32
std::string SysErrorString(int err)
{
    char buf[1024];
    /* Too bad there are three incompatible implementations of the
     * thread-safe strerror. */
    const char *s = nullptr;
#ifdef WIN32
    if (strerror_s(buf, sizeof(buf), err) == 0) s = buf;
#else
#ifdef STRERROR_R_CHAR_P /* GNU variant can return a pointer outside the passed buffer */
    s = strerror_r(err, buf, sizeof(buf));
#else /* POSIX variant always returns message in buffer */
    if (strerror_r(err, buf, sizeof(buf)) == 0) s = buf;
#endif
#endif
    std::stringstream ss;
    if (s != nullptr) {
        ss << s << " (" << err << ")";
        return ss.str();
    } else {
        ss << "Unknown error (" << err << ")";
        return ss.str();
    }
}

static std::string GetErrorReason()
{
    return SysErrorString(errno);
}

FileLock::FileLock(const fs::path& file)
{
    fd = open(file.c_str(), O_RDWR);
    if (fd == -1) {
        reason = GetErrorReason();
    }
}

FileLock::~FileLock()
{
    if (fd != -1) {
        close(fd);
    }
}

bool FileLock::TryLock()
{
    if (fd == -1) {
        return false;
    }

    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    if (fcntl(fd, F_SETLK, &lock) == -1) {
        reason = GetErrorReason();
        return false;
    }

    return true;
}
#else
std::string Win32ErrorString(int err)
{
    wchar_t buf[256];
    buf[0] = 0;
    if(FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
                       nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       buf, ARRAYSIZE(buf), nullptr))
    {
        return strprintf("%s (%d)", WideToUtf8(buf), err);
    }
    else
    {
        return strprintf("Unknown error (%d)", err);
    }
}

static std::string GetErrorReason() {
    return Win32ErrorString(GetLastError());
}

FileLock::FileLock(const fs::path& file)
{
    hFile = CreateFileW(file.wstring().c_str(),  GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        reason = GetErrorReason();
    }
}

FileLock::~FileLock()
{
    if (hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile);
    }
}

bool FileLock::TryLock()
{
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }
    _OVERLAPPED overlapped = {};
    if (!LockFileEx(hFile, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, std::numeric_limits<DWORD>::max(), std::numeric_limits<DWORD>::max(), &overlapped)) {
        reason = GetErrorReason();
        return false;
    }
    return true;
}
#endif

} // namespace fsbridge
