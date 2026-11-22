#include "VocoderEngine.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

struct Profile { const char* name; int bands; float q, attack, release, formant, sibilance, unvoiced, articulation; };
static const Profile profiles[] = {
 {"baseline",56,7,10,10,-2,.10f,.10f,1},
 {"bands40",40,7,10,10,-2,.10f,.10f,1},{"bands48",48,7,10,10,-2,.10f,.10f,1},
 {"q5",56,5,10,10,-2,.10f,.10f,1},{"q6",56,6,10,10,-2,.10f,.10f,1},{"q8",56,8,10,10,-2,.10f,.10f,1},{"q9",56,9,10,10,-2,.10f,.10f,1},
 {"attack5",56,7,5,10,-2,.10f,.10f,1},{"attack15",56,7,15,10,-2,.10f,.10f,1},{"attack20",56,7,20,10,-2,.10f,.10f,1},
 {"release5",56,7,10,5,-2,.10f,.10f,1},{"release20",56,7,10,20,-2,.10f,.10f,1},{"release30",56,7,10,30,-2,.10f,.10f,1},
 {"formant-4",56,7,10,10,-4,.10f,.10f,1},{"formant0",56,7,10,10,0,.10f,.10f,1},
 {"sibilance0",56,7,10,10,-2,0,.10f,1},{"sibilance5",56,7,10,10,-2,.05f,.10f,1},{"sibilance15",56,7,10,10,-2,.15f,.10f,1},
 {"unvoiced0",56,7,10,10,-2,.10f,0,1},{"unvoiced5",56,7,10,10,-2,.10f,.05f,1},{"unvoiced15",56,7,10,10,-2,.10f,.15f,1},
 {"articulation75",56,7,10,10,-2,.10f,.10f,.75f},{"articulation125",56,7,10,10,-2,.10f,.10f,1.25f}
};

bool render(const juce::File& inputFile, const juce::File& outputDir, const juce::String& speaker, const Profile& p) {
 juce::AudioFormatManager fm; fm.registerBasicFormats();
 std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(inputFile));
 if (!reader || reader->lengthInSamples <= 0 || reader->numChannels <= 0) return false;
 const int count = (int)juce::jmin<juce::int64>(reader->lengthInSamples,(juce::int64)(reader->sampleRate*120.0));
 const double rate=reader->sampleRate;
 juce::AudioBuffer<float> input(1,count), output(1,count);
 if (!reader->read(&input,0,count,0,true,false)) return false;
 VocoderEngine engine; engine.prepare(rate); engine.setDryWet(1); engine.setOutputGain(1);
 engine.setAttackMs(p.attack); engine.setReleaseMs(p.release); engine.setFormantShift(p.formant);
 engine.setBandCount(p.bands); engine.setFilterQ(p.q); engine.setSibilanceAmount(p.sibilance);
 engine.setUnvoicedNoise(p.unvoiced); engine.setArticulationLift(p.articulation); engine.reset();
 constexpr double pi=3.14159265358979323846;
 for(int i=0;i<count;++i) {
  const double phase=2*pi*110.0*i/rate;
  const float carrier=.13f*(float)(std::sin(phase)+.5*std::sin(2*phase)+.333333*std::sin(3*phase)+.25*std::sin(4*phase)+.2*std::sin(5*phase));
  output.setSample(0,i,engine.processSample(input.getSample(0,i),carrier,true,carrier));
 }
 auto file=outputDir.getChildFile(speaker+"_"+p.name+".wav");
 auto stream=std::unique_ptr<juce::FileOutputStream>(file.createOutputStream()); if(!stream)return false;
 juce::WavAudioFormat wav; auto writer=std::unique_ptr<juce::AudioFormatWriter>(wav.createWriterFor(stream.get(),rate,1,24,{},0));
 if(!writer)return false; stream.release(); return writer->writeFromAudioSampleBuffer(output,0,count);
}

int main() {
 const auto dir=juce::File::getCurrentWorkingDirectory().getChildFile("build").getChildFile("optimization-results");
 if(dir.createDirectory().failed())return 3;
 const juce::File audio(PRISMAVOX_TEST_AUDIO_DIR);
 for(const auto& speaker:{juce::String("Ash"),juce::String("Nova")}) {
  const auto input=audio.getChildFile(speaker=="Ash"?"vocoder_test_ash.wav":"vocoder_test_nova.wav");
  if(!input.existsAsFile())return 4;
  for(const auto& p:profiles)if(!render(input,dir,speaker,p))return 5;
 }
 std::cout<<"Rendered "<<std::size(profiles)*2<<" candidate WAV files.\n"; return 0;
}
