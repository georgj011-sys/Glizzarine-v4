#include "HarmonizerEngine.h"
#include <cstdio>
int main(){ for(float sr:{44100.f,48000.f,96000.f}) for(float f:{82.4f,110.f,196.f,330.f,523.f,880.f}){ const int N=sr*1; std::vector<float> v(N); double ph=0;
 for(int i=0;i<N;i++){ ph+=f/sr; v[i]=0.05f*(std::sin(6.283*ph)+0.5*std::sin(12.566*ph)+0.3*std::sin(18.85*ph)); }
 HarmonizerEngine h; h.prepare(sr); HarmonizerEngine::Params p; p.lowestHz=78; h.setParams(p);
 std::vector<float> a(N),b(N),c(N),d(N),e(N),g(N); h.process(v.data(),a.data(),b.data(),c.data(),d.data(),e.data(),g.data(),N);
 printf("sr %.0f f %.1f -> %.2f voiced %d (%.1f cents)\n",sr,f,h.currentPitchHz(),h.isVoiced(),1200*std::log2(h.currentPitchHz()/f)); }}
