// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Cross-check: random RDP8 compressed streams, decoded by Marconi and by FreeRDP's zgfx_decompress.
// Not built by BuildTool. From the Marconi root, with freerdp3-dev installed (Linux):
//   g++ -std=c++20 -O1 -IModules -I/usr/include/freerdp3 -I/usr/include/winpr3
//       Tools/RdpCodecCheck/zgfx_check.cpp Modules/MarconiCore/Rdp/Zgfx.cpp -lfreerdp3 -lwinpr3 -o zgfx_check
#include <MarconiCore/Rdp/Zgfx.h>
extern "C" {
#include <freerdp/codec/zgfx.h>
}
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
struct W { std::vector<uint8_t> b; int n=0; void bit(int v){ if(n%8==0) b.push_back(0); if(v) b.back()|=0x80>>(n%8); n++; }
  void bits(uint32_t v,int c){ for(int i=c-1;i>=0;--i) bit((v>>i)&1);} };
struct T{int len; uint32_t code; int vb; bool m; uint32_t base;};
static const T toks[]={{1,0,8,0,0},{5,17,5,1,0},{5,18,7,1,32},{5,19,9,1,160},{5,20,10,1,672},{5,21,12,1,1696},{6,44,14,1,5792},{6,45,15,1,22176},{7,92,18,1,54944}};
int main(){
  std::mt19937 rng(7); int fails=0;
  ZGFX_CONTEXT* z = zgfx_context_new(FALSE); Marconi::Rdp::ZgfxDecompressor mine; size_t produced=0;
  for(int msg=0;msg<300;++msg){
    W w; size_t out=0;
    int tokens = 1+rng()%200;
    for(int t=0;t<tokens;++t){
      int kind=rng()%10;
      if(kind<5 || produced+out<4){ uint8_t c=rng(); w.bits(0,1); w.bits(c,8); out++; }
      else if(kind<6){ // short literal codes
        w.bits(24,5); out++; }
      else if(kind<7){ int cnt=rng()%20; w.bits(17,5); w.bits(0,5); w.bits(cnt,15); while(w.n%8) w.n++, (void)0; for(int i=0;i<cnt;++i) w.bits(rng()&255,8); out+=cnt; }
      else { size_t avail=produced+out; uint32_t dist=1+rng()%std::min<size_t>(avail, 60000); const T* tk=nullptr;
        for(auto& c:toks) if(c.m && dist>=c.base && dist < c.base+(1u<<c.vb)) {tk=&c;break;}
        if(!tk||dist==0){continue;}
        w.bits(tk->code,tk->len); w.bits(dist-tk->base,tk->vb);
        int cnt; if(rng()%2){ w.bit(0); cnt=3; } else { int k=rng()%4; w.bit(1); int c4=4,ex=2; for(int i=0;i<k;++i){w.bit(1); c4*=2; ex++;} w.bit(0); uint32_t more=rng()%(1u<<ex); w.bits(more,ex); cnt=c4+more; }
        out+=cnt; }
      if(out>60000) break;
    }
    int pad=(8-w.n%8)%8; std::vector<uint8_t> seg{0xE0, 0x24}; seg.insert(seg.end(), w.b.begin(), w.b.end()); seg.push_back(pad);
    BYTE* dst=nullptr; UINT32 dsz=0; int r=zgfx_decompress(z, seg.data(), seg.size(), &dst, &dsz, 0);
    std::vector<uint8_t> m; std::string err; bool ok=mine.Decompress(seg.data(), seg.size(), m, err);
    if(r<0||!ok||m.size()!=dsz||memcmp(m.data(),dst,dsz)){ printf("msg %d differ: fr=%d mine=%d %s sizes %u %zu\n",msg,r,ok,err.c_str(),dsz,m.size()); fails++; }
    produced+=dsz; free(dst);
  }
  printf("zgfx cross-check: %d failures, %zu bytes\n", fails, produced); return fails;
}
