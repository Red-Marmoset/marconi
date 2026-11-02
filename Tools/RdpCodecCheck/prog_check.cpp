// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Cross-check Marconi's RemoteFX Progressive decoder against FreeRDP 3's:
//  A) real encoder output (FreeRDP's progressive_compress, what GNOME/KRdp send): photos-ish, gradients, noise;
//  B) synthetic messages with reduce-extrapolate regions, quality passes and upgrade tiles (what Windows sends),
//     built from random coefficient streams, decoded by both.
// Not built by BuildTool. From the Marconi root, with freerdp3-dev installed (Linux):
//   g++ -std=c++20 -O1 -IModules -I/usr/include/freerdp3 -I/usr/include/winpr3
//       Tools/RdpCodecCheck/prog_check.cpp Modules/MarconiCore/Rdp/Progressive.cpp
//       Modules/MarconiCore/Rdp/RemoteFx.cpp -lfreerdp3 -lwinpr3 -o prog_check
#include <MarconiCore/Rdp/Progressive.h>
extern "C" {
#include <freerdp/codec/progressive.h>
#include <freerdp/codec/color.h>
#include <freerdp/primitives.h>
}
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
using namespace Marconi::Rdp;
static std::mt19937 rng(1234);
struct W { std::vector<uint8_t> b; void u8(int v){b.push_back(v);} void u16(int v){u8(v&255);u8((v>>8)&255);} void u32(uint32_t v){u16(v&0xffff);u16(v>>16);} };
static int compare(const char* what, const std::vector<uint32_t>& mine, const std::vector<uint32_t>& theirs, int w, int h){
  int diffs=0, first=-1; for(int i=0;i<w*h;++i) if((mine[i]&0xFFFFFF)!=(theirs[i]&0xFFFFFF)){ if(first<0) first=i; diffs++; }
  if(diffs) printf("  %s: %d pixels differ (first at %d,%d: mine %06x freerdp %06x)\n", what, diffs, first%w, first/w, mine[first]&0xFFFFFF, theirs[first]&0xFFFFFF);
  return diffs?1:0;
}
static void quant(W& w, int lo, int hi){ for(int i=0;i<5;++i){ int a=lo+rng()%(hi-lo+1), b=lo+rng()%(hi-lo+1); w.u8(a|(b<<4)); } }
static std::vector<uint8_t> randomStream(size_t n, int density){ std::vector<uint8_t> v(n); for(auto& x:v){ x=0; for(int b=0;b<8;++b) if((int)(rng()%100)<density) x|=1<<b; } return v; }
int main(){
  primitives_set_hints(PRIMITIVES_PURE_SOFT); int failures=0;
  // A) encoder output
  for(int img=0; img<6; ++img){
    int w = 64*(1+rng()%6)+(img%2?17:0), h = 64*(1+rng()%4)+(img%3?9:0);
    std::vector<uint32_t> src(w*h);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x){
      uint32_t r,g,b;
      if(img<2){ r=(x*255)/w; g=(y*255)/h; b=((x+y)*3)&255; }
      else if(img<4){ r=rng()&255; g=rng()&255; b=rng()&255; }
      else { r=((x/8+y/8)%2)?250:10; g=(x*7)&255; b=200; }
      src[y*w+x]=(r<<16)|(g<<8)|b; }
    PROGRESSIVE_CONTEXT* enc=progressive_context_new(TRUE);
    BYTE* data=nullptr; UINT32 size=0;
    if(progressive_compress(enc,(BYTE*)src.data(),w*h*4,PIXEL_FORMAT_BGRX32,w,h,w*4,nullptr,&data,&size)<0){printf("encode failed\n");return 99;}
    std::vector<uint8_t> msg(data,data+size); progressive_context_free(enc);
    PROGRESSIVE_CONTEXT* dec=progressive_context_new(FALSE); progressive_create_surface_context(dec,1,w,h);
    std::vector<uint32_t> theirs(w*h,0), mine(w*h,0);
    if(progressive_decompress(dec,msg.data(),msg.size(),(BYTE*)theirs.data(),PIXEL_FORMAT_BGRX32,w*4,0,0,nullptr,1,1)<0){printf("freerdp decode failed\n");return 99;}
    ProgressiveSurface surf; std::vector<ProgressiveRect> upd; std::string err;
    if(!DecodeProgressive(msg.data(),msg.size(),surf,mine.data(),w,h,w,upd,err)){printf("mine failed: %s\n",err.c_str()); failures++; continue;}
    char label[64]; snprintf(label,64,"encoded image %d (%dx%d)",img,w,h);
    failures+=compare(label,mine,theirs,w,h);
    // and how close to the source (lossy codec): mean abs error
    long sum=0; for(int i=0;i<w*h;++i) for(int c=0;c<3;++c) sum+=abs((int)((mine[i]>>(8*c))&255)-(int)((src[i]>>(8*c))&255));
    printf("encoded image %d %dx%d: %zu bytes, mean error vs source %.2f\n",img,w,h,msg.size(),sum/(3.0*w*h));
    progressive_context_free(dec);
  }
  // B) synthetic: sync, context, frames with extrapolate regions, first tiles at a quality, upgrades
  for(int trial=0; trial<40; ++trial){
    int w=64*3, h=64*2;
    bool extrap = trial%4!=3;
    PROGRESSIVE_CONTEXT* dec=progressive_context_new(FALSE); progressive_create_surface_context(dec,1,w,h);
    ProgressiveSurface surf;
    std::vector<uint32_t> theirs(w*h,0), mine(w*h,0);
    int passes = 1+rng()%3; int density = 3+rng()%10;
    // progressive quality levels: progQuant values decreasing per pass
    int qbase = 6+rng()%3;
    for(int pass=0; pass<passes; ++pass){
      W m;
      if(pass==0){ m.u16(0xCCC0); m.u32(12); m.u32(0xCACCACCA); m.u16(0x0100);
        m.u16(0xCCC3); m.u32(10); m.u8(0); m.u16(64); m.u8(1); }
      m.u16(0xCCC1); m.u32(12); m.u32(pass); m.u16(1);
      W tiles; int ntiles=0;
      int numProg = passes; // quality i has progquant (passes-1-i)
      for(int ty=0; ty<2; ++ty) for(int tx=0; tx<3; ++tx){
        W t;
        if(pass==0){
          bool simple = (tx+ty+trial)%5==0 && passes==1;
          auto y=randomStream(40+rng()%200,density), cb=randomStream(20+rng()%100,density), cr=randomStream(20+rng()%100,density);
          t.u16(simple?0xCCC5:0xCCC6); uint32_t len=(simple?22:23)+y.size()+cb.size()+cr.size();
          t.u32(len); t.u8(0); t.u8(0); t.u8(0); t.u16(tx); t.u16(ty); t.u8(0); if(!simple) t.u8(0);
          t.u16(y.size()); t.u16(cb.size()); t.u16(cr.size()); t.u16(0);
          t.b.insert(t.b.end(),y.begin(),y.end()); t.b.insert(t.b.end(),cb.begin(),cb.end()); t.b.insert(t.b.end(),cr.begin(),cr.end());
        } else {
          std::vector<uint8_t> s[6]; for(int i=0;i<6;++i) s[i]=randomStream(rng()%60, i%2?50:density);
          size_t sum=0; for(auto& x:s) sum+=x.size();
          t.u16(0xCCC7); t.u32(26+sum); t.u8(0); t.u8(0); t.u8(0); t.u16(tx); t.u16(ty); t.u8(pass);
          for(auto& x:s) t.u16(x.size()); for(auto& x:s) t.b.insert(t.b.end(),x.begin(),x.end());
        }
        tiles.b.insert(tiles.b.end(),t.b.begin(),t.b.end()); ntiles++;
      }
      W r; r.u8(64); r.u16(2); r.u8(1); r.u8(numProg); r.u8(extrap?1:0); r.u16(ntiles); r.u32(tiles.b.size());
      r.u16(0); r.u16(0); r.u16(130); r.u16(128);  r.u16(130); r.u16(0); r.u16(62); r.u16(100);
      // quant (all bands qbase..qbase+3)
      { W q; for(int i=0;i<5;++i){ int a=qbase+(i%3), b=qbase+((i+1)%3); q.u8(a|(b<<4)); } r.b.insert(r.b.end(),q.b.begin(),q.b.end()); }
      for(int qi=0; qi<numProg; ++qi){ int pq = numProg-1-qi; r.u8(qi*30); for(int c=0;c<3;++c) for(int i=0;i<5;++i) r.u8(pq|(pq<<4)); }
      r.b.insert(r.b.end(),tiles.b.begin(),tiles.b.end());
      m.u16(0xCCC4); m.u32(6+r.b.size()); m.b.insert(m.b.end(),r.b.begin(),r.b.end());
      m.u16(0xCCC2); m.u32(6);
      int rc=progressive_decompress(dec,m.b.data(),m.b.size(),(BYTE*)theirs.data(),PIXEL_FORMAT_BGRX32,w*4,0,0,nullptr,1,pass+1);
      std::vector<ProgressiveRect> upd; std::string err;
      bool ok=DecodeProgressive(m.b.data(),m.b.size(),surf,mine.data(),w,h,w,upd,err);
      if((rc>=0)!=ok){ printf("trial %d pass %d: freerdp rc=%d, mine %s\n",trial,pass,rc,ok?"ok":err.c_str()); failures++; break; }
      char label[64]; snprintf(label,64,"synthetic trial %d (%s) pass %d",trial,extrap?"extrapolate":"classic",pass);
      failures+=compare(label,mine,theirs,w,h);
    }
    progressive_context_free(dec);
  }
  printf("progressive cross-check: %d failures\n",failures); return failures;
}
