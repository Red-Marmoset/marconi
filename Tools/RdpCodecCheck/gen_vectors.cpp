// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Emits known-answer Progressive messages and FreeRDP's (plain C) decode of them, as C++ for the unit tests.
// Not built by BuildTool. From the Marconi root, with freerdp3-dev installed (Linux):
//   g++ -std=c++20 -O1 -IModules -I/usr/include/freerdp3 -I/usr/include/winpr3
//       Tools/RdpCodecCheck/gen_vectors.cpp -lfreerdp3 -lwinpr3 -o gen_vectors
extern "C" {
#include <freerdp/codec/progressive.h>
#include <freerdp/codec/color.h>
#include <freerdp/primitives.h>
}
#include <cstdio>
#include <random>
#include <vector>
#include <cstdint>
static std::mt19937 rng(99);
struct W { std::vector<uint8_t> b; void u8(int v){b.push_back(v);} void u16(int v){u8(v&255);u8((v>>8)&255);} void u32(uint32_t v){u16(v&0xffff);u16(v>>16);} };
static uint32_t fnv(const std::vector<uint32_t>& p){ uint32_t h=2166136261u; for(uint32_t v:p){ v&=0xFFFFFF; for(int i=0;i<4;++i){ h^=(v>>(8*i))&255; h*=16777619u; } } return h; }
static void dump(const char* name, const std::vector<uint8_t>& d){ printf("const uint8_t %s[] = {", name); for(size_t i=0;i<d.size();++i) printf("%s0x%02x,", i%20==0?"\n    ":"", d[i]); printf("\n};\n"); }
static std::vector<uint8_t> rs(size_t n, int density){ std::vector<uint8_t> v(n); for(auto& x:v){ x=0; for(int b=0;b<8;++b) if((int)(rng()%100)<density) x|=1<<b; } return v; }
int main(){
  primitives_set_hints(PRIMITIVES_PURE_SOFT);
  // 1) a FreeRDP-encoded 64x48 picture (one tile, region clipped to 64x48): a gradient with a dark square.
  int w=64,h=48; std::vector<uint32_t> src(w*h);
  for(int y=0;y<h;++y) for(int x=0;x<w;++x){ uint32_t r=x*4, g=y*5, b=128; if(x>20&&x<40&&y>10&&y<30){r=g=b=30;} src[y*w+x]=(r<<16)|(g<<8)|b; }
  PROGRESSIVE_CONTEXT* enc=progressive_context_new(TRUE); BYTE* data=nullptr; UINT32 size=0;
  progressive_compress(enc,(BYTE*)src.data(),w*h*4,PIXEL_FORMAT_BGRX32,w,h,w*4,nullptr,&data,&size);
  std::vector<uint8_t> m1(data,data+size); progressive_context_free(enc);
  PROGRESSIVE_CONTEXT* dec=progressive_context_new(FALSE); progressive_create_surface_context(dec,1,w,h);
  std::vector<uint32_t> out(w*h,0); progressive_decompress(dec,m1.data(),m1.size(),(BYTE*)out.data(),PIXEL_FORMAT_BGRX32,w*4,0,0,nullptr,1,1);
  dump("kEncodedTile",m1); printf("// FreeRDP decodes it to FNV-1a 0x%08x; pixel (5,5)=%06x (30,20)=%06x\n", fnv(out), out[5*w+5]&0xFFFFFF, out[20*w+30]&0xFFFFFF);
  progressive_context_free(dec);
  // 2) synthetic first pass (quality 0) + upgrade (quality 1) on a 64x64 surface, reduce-extrapolate.
  w=64; h=64; dec=progressive_context_new(FALSE); progressive_create_surface_context(dec,1,w,h); out.assign(w*h,0);
  for(int pass=0; pass<2; ++pass){
    W m; if(pass==0){ m.u16(0xCCC0); m.u32(12); m.u32(0xCACCACCA); m.u16(0x0100); m.u16(0xCCC3); m.u32(10); m.u8(0); m.u16(64); m.u8(1); }
    m.u16(0xCCC1); m.u32(12); m.u32(pass); m.u16(1);
    W t;
    if(pass==0){ auto y=rs(48,8), cb=rs(24,6), cr=rs(24,6); t.u16(0xCCC6); t.u32(23+y.size()+cb.size()+cr.size()); t.u8(0);t.u8(0);t.u8(0); t.u16(0);t.u16(0); t.u8(0); t.u8(0); t.u16(y.size()); t.u16(cb.size()); t.u16(cr.size()); t.u16(0); for(auto* v:{&y,&cb,&cr}) t.b.insert(t.b.end(),v->begin(),v->end()); }
    else { std::vector<uint8_t> s[6]; for(int i=0;i<6;++i) s[i]=rs(10+rng()%14, i%2?50:8); size_t sum=0; for(auto& x:s) sum+=x.size(); t.u16(0xCCC7); t.u32(26+sum); t.u8(0);t.u8(0);t.u8(0); t.u16(0);t.u16(0); t.u8(1); for(auto& x:s) t.u16(x.size()); for(auto& x:s) t.b.insert(t.b.end(),x.begin(),x.end()); }
    W r; r.u8(64); r.u16(1); r.u8(1); r.u8(2); r.u8(1); r.u16(1); r.u32(t.b.size());
    r.u16(0); r.u16(0); r.u16(64); r.u16(64);
    for(int i=0;i<5;++i) r.u8(7|(8<<4));
    for(int q=0;q<2;++q){ int pq=q==0?2:0; r.u8(q*50); for(int c=0;c<15;++c) r.u8(pq|(pq<<4)); }
    r.b.insert(r.b.end(),t.b.begin(),t.b.end());
    m.u16(0xCCC4); m.u32(6+r.b.size()); m.b.insert(m.b.end(),r.b.begin(),r.b.end()); m.u16(0xCCC2); m.u32(6);
    int rc=progressive_decompress(dec,m.b.data(),m.b.size(),(BYTE*)out.data(),PIXEL_FORMAT_BGRX32,w*4,0,0,nullptr,1,pass+1);
    dump(pass==0?"kFirstPass":"kUpgradePass", m.b); printf("// rc %d; FreeRDP after this pass: FNV-1a 0x%08x; pixel (10,10)=%06x\n", rc, fnv(out), out[10*w+10]&0xFFFFFF);
  }
  progressive_context_free(dec);
}
