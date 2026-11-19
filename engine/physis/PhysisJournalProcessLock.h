// PHYSIS: process-scoped exclusive ownership of the admin receipt journal.
// The lock is advisory: all PHYSIS writers must use the same lock path.
// No gameplay, genre, editor, water or terrain dependency.
#pragma once
#include <filesystem>
#include <string>
#include <utility>
#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace aiverse::physis::remote {

class JournalProcessLock {
public:
    JournalProcessLock() = default;
    JournalProcessLock(const JournalProcessLock&) = delete;
    JournalProcessLock& operator=(const JournalProcessLock&) = delete;
    ~JournalProcessLock() { release(); }

    bool acquire(const std::filesystem::path& journal, std::string* error=nullptr) {
        if(held()) {
            if(error)*error="PHYSIS journal lock already held by this endpoint";
            return false;
        }
        const auto parent=journal.parent_path();
        if(parent.empty() || !std::filesystem::is_directory(parent)) {
            if(error)*error="PHYSIS journal lock parent directory missing";
            return false;
        }
        const auto path=std::filesystem::path(journal.string()+".lock");
#ifndef _WIN32
        int flags=O_RDWR|O_CREAT;
#ifdef O_CLOEXEC
        flags|=O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        flags|=O_NOFOLLOW;
#endif
        const int fd=::open(path.c_str(),flags,0600);
        if(fd<0) {
            if(error)*error="PHYSIS journal lock file open failed";
            return false;
        }
        // The lock must be an ordinary server-owned inode, not a FIFO,
        // device or hardlink into another directory. O_NOFOLLOW (where
        // supported) rejects a final-component symlink at open time.
        struct stat lockStat{};
        if(::fstat(fd,&lockStat)!=0 || !S_ISREG(lockStat.st_mode) ||
           lockStat.st_nlink!=1 || lockStat.st_uid!=::geteuid() ||
           (lockStat.st_mode & 0077)!=0) {
            ::close(fd);
            if(error)*error="PHYSIS journal lock inode is unsafe";
            return false;
        }
        if(::flock(fd,LOCK_EX|LOCK_NB)!=0) {
            ::close(fd);
            if(error)*error="PHYSIS journal is owned by another server";
            return false;
        }
        fd_=fd;
#else
        HANDLE handle=CreateFileW(path.wstring().c_str(),GENERIC_READ|GENERIC_WRITE,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE) {
            if(error)*error="PHYSIS journal lock file open failed";
            return false;
        }
        OVERLAPPED overlapped{};
        if(!LockFileEx(handle,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,
                       0,1,0,&overlapped)) {
            CloseHandle(handle);
            if(error)*error="PHYSIS journal is owned by another server";
            return false;
        }
        handle_=handle;
#endif
        if(error)error->clear();
        return true;
    }

    bool held() const {
#ifndef _WIN32
        return fd_>=0;
#else
        return handle_!=INVALID_HANDLE_VALUE;
#endif
    }

    void release() {
#ifndef _WIN32
        if(fd_>=0) {
            ::flock(fd_,LOCK_UN);
            ::close(fd_);
            fd_=-1;
        }
#else
        if(handle_!=INVALID_HANDLE_VALUE) {
            OVERLAPPED overlapped{};
            UnlockFileEx(handle_,0,1,0,&overlapped);
            CloseHandle(handle_);
            handle_=INVALID_HANDLE_VALUE;
        }
#endif
    }
private:
#ifndef _WIN32
    int fd_=-1;
#else
    HANDLE handle_=INVALID_HANDLE_VALUE;
#endif
};

} // namespace aiverse::physis::remote
