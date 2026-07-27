#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include "Source/DSP/PitchShifter.h"
#include "Source/DSP/ShredWidthEngine.h"
#include <cmath>
#include <vector>
#include <algorithm>

namespace {
constexpr double sr = 48000.0;
constexpr double pi = 3.14159265358979323846;

class CorrectnessTests final : public juce::UnitTest {
public:
 CorrectnessTests() : juce::UnitTest("Pitch and correlation correctness", "ShredWidth/Correctness") {}
 void runTest() override { pitchAccuracy(); correlationSanity(); }
 void pitchAccuracy() {
  beginTest("PitchShifter FFT peak matches cents ratio");
  // 16384-point radix-2-equivalent direct DFT, rectangular window; the peak
  // is searched on a ±2-bin neighborhood. The tolerance is 25 cents, allowing
  // granular transient/leakage while detecting a wrong pitch direction.
  constexpr int n=16384; constexpr double f=1000.0, cents=100.0;
  PitchShifter p; p.prepare(sr,512); p.setDetuneCents((float)cents);
  std::vector<float> x(n); for(int i=0;i<n;++i) x[i]=(float)std::sin(2*pi*f*i/sr);
  p.process(x.data(), n);
  int best=1; double bm=0; for(int k=1;k<n/2;++k) { double re=0,im=0; for(int i=2048;i<n;++i){double a=2*pi*k*i/n; re+=x[i]*std::cos(a); im-=x[i]*std::sin(a);} double m=re*re+im*im; if(m>bm){bm=m;best=k;} }
  double measured=best*sr/n, expected=f*std::pow(2.0,cents/1200.0);
  expect(std::abs(1200.0*std::log2(measured/expected)) <= 25.0, "FFT peak is within 25 cents of expected shifted frequency");
 }
 void correlationSanity() {
  beginTest("Width and detune correlation sanity");
  auto run=[&](float width,float detune){ ShredWidthEngine e; e.setMix(1); e.setWidth(width); e.setDetuneCents(detune); e.setDelayMsL(1); e.setDelayMsR(1); e.prepare(sr,512); std::vector<float> l(24000),r(24000); for(int i=0;i<(int)l.size();++i) l[i]=r[i]=(float)std::sin(2*pi*440*i/sr); e.processBlock(l.data(),r.data(),(int)l.size()); return e.getMeterTap().getCorrelation(); };
  expect(run(2,25) < 0.98f, "wide, detuned signal correlation drops after latency/settling");
  expect(run(0,25) > 0.95f, "zero width remains essentially mono");
  beginTest("Folded mono retains sub-crossover energy");
  ShredWidthEngine e; e.setMix(1); e.setWidth(2); e.setDetuneCents(25); e.setCrossoverHz(180); e.prepare(sr,512); std::vector<float> l(24000),r(24000); for(int i=0;i<(int)l.size();++i) l[i]=r[i]=(float)std::sin(2*pi*100*i/sr); e.processBlock(l.data(),r.data(),(int)l.size()); double energy=0; for(int i=2048;i<(int)l.size();++i){double v=l[i]+r[i];energy+=v*v;} expect(energy>1e-3,"folded mono retains below-crossover energy");
 }
};
CorrectnessTests correctnessTests;
}
