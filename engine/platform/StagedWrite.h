#pragma once
// Crash-safe document publishing for platforms without the POSIX *at() calls (Windows). The POSIX code paths in
// Studio documents stage descriptor-relative (openat / renameat); this is the same contract with
// std::filesystem and the CRT: the folder must not be a symlink / junction, the staging file is created
// exclusively (an existing file or link fails, nothing is followed or truncated), its bytes are flushed to disk,
// and only then is it renamed over the live file. On failure the staging file is removed; the live file is untouched.
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace aiverse::platform {

// A real directory, not a symlink or junction.
inline bool plainDirectory(const std::filesystem::path& dir) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(dir, ec);
    return !ec && std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status);
}

// Creates `file` exclusively, writes `bytes` and flushes them to disk. Throws runtime_error mentioning `what`.
inline void writeNewFileDurably(const std::filesystem::path& file, const std::string& bytes, const std::string& what) {
#ifdef _WIN32
    const int fd = ::_wopen(file.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT, _S_IREAD | _S_IWRITE);
#else
    const int fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
#endif
    if (fd < 0) throw std::runtime_error("Cannot create exclusive " + what + " staging file");
    std::size_t offset = 0;
    bool ok = true;
    while (ok && offset < bytes.size()) {
        const std::size_t chunk = std::min<std::size_t>(bytes.size() - offset, 1u << 30);
#ifdef _WIN32
        const int n = ::_write(fd, bytes.data() + offset, unsigned(chunk));
#else
        const auto n = ::write(fd, bytes.data() + offset, chunk);
#endif
        if (n <= 0) ok = false;
        else offset += std::size_t(n);
    }
#ifdef _WIN32
    const bool flushed = ok && ::_commit(fd) == 0; // FlushFileBuffers
    const bool closed = ::_close(fd) == 0;
#else
    const bool flushed = ok && ::fsync(fd) == 0;
    const bool closed = ::close(fd) == 0;
#endif
    if (!ok || !flushed || !closed) {
        std::error_code ec;
        std::filesystem::remove(file, ec);
        throw std::runtime_error(!ok ? "Failed writing " + what + " staging file" : "Failed flushing " + what + " staging file");
    }
}

// Publishes `bytes` as dir/filename through dir/pendingName (see the header comment).
inline void stagedWrite(const std::filesystem::path& dir, const std::string& filename, const std::string& pendingName,
                        const std::string& bytes, const std::string& what) {
    if (!plainDirectory(dir)) throw std::runtime_error("Cannot open " + what + " folder");
    const std::filesystem::path pending = dir / pendingName;
    writeNewFileDurably(pending, bytes, what);
    std::error_code ec;
    std::filesystem::rename(pending, dir / filename, ec); // MoveFileEx(REPLACE_EXISTING) on Windows
    if (ec) {
        std::error_code ignore;
        std::filesystem::remove(pending, ignore);
        throw std::runtime_error("Cannot publish " + what + " file");
    }
}

} // namespace aiverse::platform
