// Native POSIX persistence smoke without ECS, networking or editor linkage.
#include "engine/physis/PhysisJournalPosixIO.h"
#ifndef _WIN32
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs=std::filesystem;
namespace io=aiverse::physis::remote::posix_journal;
static int failures=0;
#define CHECK(expr) do {if(!(expr)) {std::printf("PHYSIS POSIX JOURNAL FAIL %s:%d: %s\n",__FILE__,__LINE__,#expr);++failures;}} while(0)

int main() {
    std::string pattern=(fs::temp_directory_path()/"physis-posix-XXXXXX").string();
    char* rootName=::mkdtemp(pattern.data());
    if(!rootName)return 2;
    const fs::path root=rootName, parent=root/"journal-dir", moved=root/"moved";
    fs::create_directory(parent);
    CHECK(::chmod(parent.c_str(),0700)==0);
    int flags=O_RDONLY;
#ifdef O_DIRECTORY
    flags|=O_DIRECTORY;
#endif
#ifdef O_CLOEXEC
    flags|=O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags|=O_NOFOLLOW;
#endif
    const int dir=::open(parent.c_str(),flags);
    if(dir<0) {fs::remove_all(root);return 2;}
    std::string reason;
    std::vector<std::uint8_t> data{9,1,4,7,2,0,8,3};
    std::vector<std::uint8_t> readback{77};
    bool missing=false;
    CHECK(io::validBasename("receipts"));
    CHECK(!io::validBasename("../escape"));
    CHECK(!io::validBasename("."));
    CHECK(io::read(dir,"receipts",1,1024,readback,missing,&reason));
    CHECK(missing && readback==std::vector<std::uint8_t>{77});
    CHECK(io::write(dir,"receipts",data,"nonce-a",&reason));
    CHECK(io::read(dir,"receipts",1,1024,readback,missing,&reason));
    CHECK(!missing && readback==data);
    CHECK(::chmod((parent/"receipts").c_str(),0644)==0);
    CHECK(!io::read(dir,"receipts",1,1024,readback,missing,&reason));
    CHECK(::chmod((parent/"receipts").c_str(),0600)==0);
    CHECK(io::read(dir,"receipts",1,1024,readback,missing,&reason));
    CHECK(!io::read(dir,"receipts",9,1024,readback,missing,&reason));
    CHECK(!io::write(dir,"../wrong",data,"nonce-a",&reason));
    CHECK(!io::write(dir,"receipts",data,"../wrong",&reason));
    CHECK(fs::remove(parent/"receipts"));
    CHECK(::mkfifo((parent/"receipts").c_str(),0600)==0);
    CHECK(!io::read(dir,"receipts",1,1024,readback,missing,&reason));
    CHECK(fs::remove(parent/"receipts"));
    const fs::path target=root/"somewhere-else";
    CHECK(::symlink(target.c_str(),(parent/"receipts").c_str())==0);
    CHECK(!io::read(dir,"receipts",1,1024,readback,missing,&reason));
    CHECK(fs::remove(parent/"receipts"));
    CHECK(::chmod(parent.c_str(),0777)==0);
    CHECK(!io::write(dir,"receipts",data,"nonce-b",&reason));
    CHECK(::chmod(parent.c_str(),0700)==0);
    CHECK(io::write(dir,"receipts",data,"nonce-c",&reason));
    fs::rename(parent,moved);
    fs::create_directory(parent);
    CHECK(::chmod(parent.c_str(),0700)==0);
    CHECK(io::write(dir,"receipts",data,"nonce-d",&reason));
    CHECK(fs::exists(moved/"receipts"));
    CHECK(!fs::exists(parent/"receipts"));
    CHECK(io::read(dir,"receipts",1,1024,readback,missing,&reason));
    CHECK(readback==data);
    CHECK(::close(dir)==0);
    fs::remove_all(root);
    std::printf("PHYSIS POSIX journal I/O: %s\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
#else
int main(){return 0;}
#endif
