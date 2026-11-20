#include "HarmonizerEngine.h"
#include "Sequencer.h"
#include <cstdio>
#include <random>
int main(){ const float sr=48000; std::mt19937 g(7); std::normal_distribution<float> nd(0,1);
 for(int scen=0;scen<6;scen++){
 const int N=sr*40; std::vector<float> v(N); double ph=0;
 for(int i=0;i<N;i++){ float t=i/sr; float x=0;
   switch(scen){ case 0: x= t<10?0:4.0f*std::sin(6.283*ph); ph+=220/sr; break;          // silence then clipping-loud
     case 1: x= 0.5f*nd(g); break;                                                       // pure noise
     case 2: x= 0.3f + 0.0f*t; break;                                                    // DC
     case 3: ph+= (60+ 1500*(0.5+0.5*std::sin(t)))/sr; x=0.5f*std::sin(6.283*ph); break;  // sweeping pitch 60..1560
     case 4: x= (i%4800<10)?1.0f:0.0f; break;                                            // clicks
     case 5: x= std::fmod(t,1.0f)<0.5f? 1e-9f*nd(g): 0.4f*std::sin(6.283*(ph+=140/sr)); break; }
   v[i]=x; }
 HarmonizerEngine h; h.prepare(sr); HarmonizerEngine::Params p; p.blend=0.5f; p.subOctave=0.5f; p.follow=1; p.leveling=1; p.sustain=8; h.setParams(p);
 Sequencer q(h); q.setSampleRate(sr); q.setMode(scen%3); q.setStepSamples(sr*0.1); q.setStyle(scen%3,0.2f,0.8f);
 std::vector<float> L(N),R(N),D(N),DD(N),SL(N),SR(N); int pos=0,c=-1; int bad=0; double mx=0;
 while(pos<N){ int bs=std::min(N-pos,(int)(16+g()%4096)); float t=pos/sr; int cc=(int)(t/1.5f); if(cc!=c){ c=cc; q.allOff(); for(int k:{0,3,7,10,14}) q.keyDown(40+(cc*5)%40+k,(g()%127)/127.f);} 
   int done=0; while(done<bs){ int s=std::min(bs-done,std::max(1,q.samplesToNextEvent())); h.process(v.data()+pos+done,L.data()+pos+done,R.data()+pos+done,D.data()+pos+done,DD.data()+pos+done,SL.data()+pos+done,SR.data()+pos+done,s); q.advance(s); q.fire(); done+=s; }
   pos+=bs; }
 for(int i=0;i<N;i++){ for(float y:{L[i],R[i],DD[i],SL[i]}){ if(!std::isfinite(y)) bad++; else mx=std::max(mx,(double)std::abs(y)); } }
 printf("scen %d bad %d max %.2f\n",scen,bad,mx); }
}
