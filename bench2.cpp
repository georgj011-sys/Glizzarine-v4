#define _ALLOW_KEYWORD_MACROS 1   // MSVC: allow the test-only trick below
#define private public
#include "HarmonizerEngine.h"
#include <cstdio>
#include <chrono>
#include <algorithm>
int main(){ const float sr=48000; const int N=sr*20; std::vector<float> v(N); double ph=0;
 for(int i=0;i<N;i++){ ph+=196/sr; v[i]=0.2f*std::sin(6.283*ph)+0.1f*std::sin(12.566*ph); }
 HarmonizerEngine h; h.prepare(sr); HarmonizerEngine::Params p; p.lowestHz=78; h.setParams(p);
 for(int k:{48,52,55,59,62,64,67,71}) h.noteOn(k,0.8f);
 std::vector<float> L(N),R(N),D(N),DD(N),SL(N),SR(N); std::vector<double> t;
 for(int pos=0;pos<N;pos+=64){ auto a=std::chrono::steady_clock::now(); h.process(v.data()+pos,L.data()+pos,R.data()+pos,D.data()+pos,DD.data()+pos,SL.data()+pos,SR.data()+pos,64); t.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-a).count());}
 std::vector<double> s=t; std::sort(s.begin(),s.end()); printf("median %.0f p99 %.0f p999 %.0f max %.0f\n",s[s.size()/2],s[s.size()*99/100],s[s.size()*999/1000],s.back());
 int c=0; for(size_t i=0;i<t.size()&&c<10;i++) if(t[i]>600){ printf("block %zu (%.2fs) %.0f us\n",i,i*64/sr,t[i]); c++; }
 auto a=std::chrono::steady_clock::now(); for(int i=0;i<200;i++) h.detectPitch(); printf("detectPitch %.0f us\n", std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-a).count()/200);
}
