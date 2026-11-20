#include "HarmonizerEngine.h"
#include "Sequencer.h"
#include <cstdio>
#include <random>
struct Reso { float b0=0,a1=0,a2=0,y1=0,y2=0; void set(float f,float bw,float sr){ float r=std::exp(-3.14159f*bw/sr); a1=2*r*std::cos(2*3.14159f*f/sr); a2=-r*r; b0=1-r; } float p(float x){ float y=b0*x+a1*y1+a2*y2; y2=y1; y1=y; return y; } };
std::vector<float> voice(float sr,int N,float lv){ std::vector<float> v(N); Reso rs[3]; double ph=0; const float F[3]={700,1150,2500}; float pr=0; std::mt19937 g(3); std::normal_distribution<float> nd(0,1);
  for(int i=0;i<N;i++){ float t=i/sr; float amp=(t>0.1f&&t<N/sr-0.1f)? lv:0; ph+=174.6*(1+0.01*std::sin(2*3.14159*5*t))/sr; if(ph>=1)ph-=1;
   float y=amp*(std::sin(6.2831853*ph)+0.5f*std::sin(2*6.2831853*ph)+0.25f*std::sin(3*6.2831853*ph)); v[i]=y*0.3f; (void)pr; (void)rs; (void)F; (void)nd; (void)g;} return v; }
// click measure: largest second difference relative to the median second difference
double clicks(const std::vector<float>& x){ std::vector<double> d; double mx=0; for(size_t i=2;i<x.size();i++){ double a=std::abs(x[i]-2*x[i-1]+x[i-2]); d.push_back(a); mx=std::max(mx,a);} std::nth_element(d.begin(),d.begin()+d.size()*0.99,d.end()); return mx/(d[d.size()*0.99]+1e-12); }
int main(){ const float sr=44100; const int N=sr*6;
  for (int blend : {0, 1}) for (int mode : {0,1,2}) {
    auto v=voice(sr,N,0.3f); HarmonizerEngine h; h.prepare(sr); HarmonizerEngine::Params p; p.dry=0; p.release=0.08f; p.attack=0.005f; p.blend = blend ? 1.0f : 0.0f; h.setParams(p);
    Sequencer q(h); q.setSampleRate(sr); q.setMode(mode); q.setOrder(2); q.setSteps(7); q.setStepSamples(sr*0.125); q.setGate(0.5f); q.setStyle(0, 0.08f, 0.7f);
    std::vector<float> L(N),R(N),D(N),DD(N),SL(N),SR(N); int pos=0; int phase=0;
    while(pos<N){
      float t=pos/sr;
      if(phase==0&&t>=0.3){ for(int k:{53,57,60,64}) q.keyDown(k,0.8f); phase=1; }
      if(phase==1&&t>=1.8){ for(int k:{53,57,60,64}) q.keyUp(k); for(int k:{55,59,62,65,69}) q.keyDown(k,0.6f); phase=2; }
      if(phase==2&&t>=3.3){ for(int k:{55,59,62,65,69}) q.keyUp(k); phase=3; }
      if(phase==3&&t>=3.6){ for(int k=48;k<48+18;k++) q.keyDown(k,0.7f); phase=4; }   // 18 keys: forces voice stealing
      if(phase==4&&t>=5.0){ for(int k=48;k<48+18;k++) q.keyUp(k); phase=5; }
      int nx=std::min(N, pos+std::max(1,std::min(64,q.samplesToNextEvent())));
      h.process(v.data()+pos,L.data()+pos,R.data()+pos,D.data()+pos,DD.data()+pos,SL.data()+pos,SR.data()+pos,nx-pos); q.advance(nx-pos); pos=nx; q.fire(); }
    bool nan=false; float pk=0; for(float x:L){ if(std::isnan(x)) nan=true; pk=std::max(pk,std::abs(x)); }
    if(mode==2){ FILE*f=fopen(blend?"m2v.f32":"m2.f32","wb"); fwrite(L.data(),4,N,f); fclose(f);} 
    printf("%s mode %d: click ratio %.1f  peak %.2f nan %d\n", blend?"vocoder ":"harmoniz", mode, clicks(L), pk, (int)nan);
  }
}
