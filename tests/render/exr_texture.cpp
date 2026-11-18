// Synthetic, dependency-free OpenEXR scanline HALF/FLOAT parser fixture.
// These are authored tests; their presence does not imply they have been run.
#include "engine/render/ExrTexture.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse::render;
namespace {
void expect(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
void u32(std::string& s,uint32_t n){for(int i=0;i<4;++i)s+=char(n>>(8*i));}
void u64(std::string& s,uint64_t n){for(int i=0;i<8;++i)s+=char(n>>(8*i));}
void patch64(std::string& s,size_t p,uint64_t n){for(int i=0;i<8;++i)s[p+size_t(i)]=char(n>>(8*i));}
void cstr(std::string& s,const char* name){s+=name;s+='\0';}
void attr(std::string& s,const char* name,const char* type,const std::string& data){
    cstr(s,name);cstr(s,type);u32(s,uint32_t(data.size()));s+=data;
}
void half(std::string& s,uint16_t h){s+=char(h);s+=char(h>>8);}
void real(std::string& s,float f){uint32_t v;std::memcpy(&v,&f,4);u32(s,v);}
std::string fixture(bool compressed=false,bool alpha=false){
    std::string s;
    u32(s,20000630u);u32(s,2u); // magic, version 2 no extra flags
    std::string channels;
    // Out-of-order channel names, mixed planar FLOAT/HALF.
    cstr(channels,"G");u32(channels,1);u32(channels,0);u32(channels,1);u32(channels,1);
    cstr(channels,"B");u32(channels,2);u32(channels,0);u32(channels,1);u32(channels,1);
    cstr(channels,"R");u32(channels,1);u32(channels,0);u32(channels,1);u32(channels,1);
    if(alpha){cstr(channels,"A");u32(channels,1);u32(channels,0);u32(channels,1);u32(channels,1);}
    channels+='\0';
    attr(s,"channels","chlist",channels);
    attr(s,"compression","compression",std::string(1,char(compressed?3:0)));
    std::string window;
    u32(window,uint32_t(int32_t(-3)));u32(window,uint32_t(int32_t(-2)));
    u32(window,uint32_t(int32_t(-2)));u32(window,uint32_t(int32_t(-1)));
    attr(s,"dataWindow","box2i",window);
    s+='\0'; // attribute list terminator
    const size_t offsets=s.size();
    s.resize(s.size()+16,'\0');
    for(int row=1;row>=0;--row){ // physically reverse-ordered scanline chunks
        patch64(s,offsets+size_t(row)*8,uint64_t(s.size()));
        u32(s,uint32_t(int32_t(-2+row)));
        u32(s,uint32_t(alpha?20:16)); // two G halves, two B floats, two R halves [+ A halves]
        for(int x=0;x<2;++x)half(s,row?0x4000u:0x3c00u); // green 2 / 1
        for(int x=0;x<2;++x)real(s,row?-3.5f:4.0f); // blue negative is preserved
        for(int x=0;x<2;++x)half(s,x?0x3800u:0x3c00u); // red 0.5 / 1
        if(alpha)for(int x=0;x<2;++x)half(s,0x3800u);
    }
    return s;
}
void dimensionsAndColors(){
    RadianceImage img;std::string error;
    const auto s=fixture();
    expect(parseOpenExr(s,img,error)&&error.empty(),"valid reverse-ordered scanlines");
    expect(img.width==2&&img.height==2&&img.rgba16f.size()==16,"output dimensions");
    expect(img.rgba16f[0]==0x3c00&&img.rgba16f[1]==0x3c00&&
           img.rgba16f[2]==0x4400&&img.rgba16f[3]==0x3c00,
           "top-left RGBA reconstructs planar channels");
    expect(img.rgba16f[8]==0x3c00&&img.rgba16f[9]==0x4000&&
           img.rgba16f[10]==uint16_t(0xc300u)&&img.rgba16f[11]==0x3c00,
           "negative HDR and physical reverse scanline order");
    expect(parseOpenExr(fixture(false,true),img,error) &&
           img.rgba16f[3]==0x3800u,"optional alpha is parsed");
}
void invalidFiles(){
    RadianceImage img;std::string err;
    auto data=fixture(true);
    expect(!parseOpenExr(data,img,err)&&!err.empty()&&img.rgba16f.empty(),
           "unsupported compressed EXR refuses");
    data=fixture();
    data.pop_back();
    expect(!parseOpenExr(data,img,err)&&!err.empty(),"truncated scanline refuses");
    data=fixture();
    data[0]='a';
    expect(!parseOpenExr(data,img,err)&&!err.empty(),"bad magic refuses");
    data=fixture();
    data[4]=char(3);
    expect(!parseOpenExr(data,img,err)&&!err.empty(),"unsupported version refuses");
    data=fixture();
    // Both scanline offsets now target row zero: duplicate row index rejects.
    // Derive the offset table position by the first offset that references
    // a valid scanline chunk rather than hardcoding header offsets.
    size_t offsetAt=0;
    for(size_t p=8;p+16<data.size();++p){
        uint64_t a=0,b=0;
        if(!exr_detail::u64(data,p,a)||!exr_detail::u64(data,p+8,b))continue;
        if(a>p+16 && a<data.size() && b>a && b<data.size()){
            uint32_t yA=0,yB=0;
            if(exr_detail::u32(data,size_t(a),yA) &&
               exr_detail::u32(data,size_t(b),yB) &&
               int32_t(yA)==-2 && int32_t(yB)==-1){offsetAt=p;break;}
        }
    }
    expect(offsetAt!=0,"synthetic offset table located");
    uint64_t first=0;exr_detail::u64(data,offsetAt,first);
    patch64(data,offsetAt+8,first);
    expect(!parseOpenExr(data,img,err)&&!err.empty(),
           "duplicate physical scanline offset refuses");
}
}
int main(){
    try{dimensionsAndColors();invalidFiles();}
    catch(const std::exception& e){
        std::fprintf(stderr,"EXR texture fixture FAILED: %s\n",e.what());return 1;
    }
    std::puts("OpenEXR scanline import fixture: OK");return 0;
}
