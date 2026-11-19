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
        // Anchor the basename lookup to a verified directory descriptor.
        // A world/group-writable or symlinked parent can let another user
        // replace the lock inode and bypass advisory flock ownership.
        int parentFlags=O_RDONLY;
#ifdef O_DIRECTORY
        parentFlags|=O_DIRECTORY;
#endif
#ifdef O_CLOEXEC
        parentFlags|=O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        parentFlags|=O_NOFOLLOW;
#endif
        const int parentFd=::open(parent.c_str(),parentFlags);
        if(parentFd<0) {
            if(error)*error="PHYSIS journal lock parent open failed";
            return false;
        }
        struct stat parentStat{};
        if(::fstat(parentFd,&parentStat)!=0 ||
           !S_ISDIR(parentStat.st_mode) ||
           parentStat.st_uid!=::geteuid() ||
           (parentStat.st_mode & 0022)!=0) {
            ::close(parentFd);
            if(error)*error="PHYSIS journal lock parent is not server-owned or is writable by others";
            return false;
        }
        // Nonblocking open is essential before fstat(): a malicious FIFO
        // at the lock path must not hang the privileged admin server.
        // The eventual validated regular-file descriptor is unaffected.
        int flags=O_RDWR|O_CREAT|O_NONBLOCK;
#ifdef O_CLOEXEC
        flags|=O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        flags|=O_NOFOLLOW;
#endif
        const auto basename=path.filename();
        const int fd=::openat(parentFd,basename.c_str(),flags,0600);
        if(fd<0) {
            ::close(parentFd);
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
            ::close(parentFd);
            if(error)*error="PHYSIS journal lock inode is unsafe";
            return false;
        }
        if(::flock(fd,LOCK_EX|LOCK_NB)!=0) {
            ::close(fd);
            ::close(parentFd);
            if(error)*error="PHYSIS journal is owned by another server";
            return false;
        }
        // Validate the visible name again AFTER locking. If another
        // actor unlinked/replaced it during acquisition, the lock fd
        // may protect a different inode than the path future owners open.
        struct stat visible{};
        struct stat visibleParent{};
        const bool samePath=
            ::fstatat(parentFd,basename.c_str(),&visible,AT_SYMLINK_NOFOLLOW)==0 &&
            S_ISREG(visible.st_mode) &&
            visible.st_dev==lockStat.st_dev &&
            visible.st_ino==lockStat.st_ino &&
            visible.st_nlink==1 &&
            ::lstat(parent.c_str(),&visibleParent)==0 &&
            S_ISDIR(visibleParent.st_mode) &&
            visibleParent.st_dev==parentStat.st_dev &&
            visibleParent.st_ino==parentStat.st_ino;
        ::close(parentFd);
        if(!samePath) {
            ::flock(fd,LOCK_UN);
            ::close(fd);
            if(error)*error="PHYSIS journal lock path changed during acquisition";
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
