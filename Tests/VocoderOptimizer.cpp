#include "VocoderEngine.h"
#include "CarrierSynth.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

struct Profile { const char* name; int bands; float q, attack, release, formant, sibilance, unvoiced, articulation; };
static const Profile profiles[] = {
 {"r01_fp0",56,7,10,1,0,.10f,.10f,1},
 {"r01_fp0p5",56,7,10,1,0.5,.10f,.10f,1},
 {"r01_fp1",56,7,10,1,1,.10f,.10f,1},
 {"r01_fp1p5",56,7,10,1,1.5,.10f,.10f,1},
 {"r015_fp0",56,7,10,1.5,0,.10f,.10f,1},
 {"r015_fp0p5",56,7,10,1.5,0.5,.10f,.10f,1},
 {"r015_fp1",56,7,10,1.5,1,.10f,.10f,1},
 {"r015_fp1p5",56,7,10,1.5,1.5,.10f,.10f,1},
 {"r02_fp0",56,7,10,2,0,.10f,.10f,1},
 {"r02_fp0p5",56,7,10,2,0.5,.10f,.10f,1},
 {"r02_fp1",56,7,10,2,1,.10f,.10f,1},
 {"r02_fp1p5",56,7,10,2,1.5,.10f,.10f,1},
 {"r025_fp0",56,7,10,2.5,0,.10f,.10f,1},
 {"r025_fp0p5",56,7,10,2.5,0.5,.10f,.10f,1},
 {"r025_fp1",56,7,10,2.5,1,.10f,.10f,1},
 {"r025_fp1p5",56,7,10,2.5,1.5,.10f,.10f,1}
};

bool render(const juce::File& inputFile, const juce::File& outputDir, const juce::String& speaker, const Profile& p,
            float preEmphasis = 0.15f) {
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
 engine.setUnvoicedNoise(p.unvoiced); engine.setArticulationLift(p.articulation);
 engine.setPreEmphasisAmount(preEmphasis); engine.reset();
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

bool renderCarrierComparison(const juce::File& inputFile, const juce::File& outputDir,
                             const juce::String& speaker, const juce::String& profile,
                             int waveform)
{
 juce::AudioFormatManager fm; fm.registerBasicFormats();
 std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(inputFile));
 if (!reader || reader->lengthInSamples <= 0 || reader->numChannels <= 0) return false;
 const int count=(int)juce::jmin<juce::int64>(reader->lengthInSamples,(juce::int64)(reader->sampleRate*120.0));
 const double rate=reader->sampleRate;
 juce::AudioBuffer<float> input(1,count), carrier(1,count), output(1,count);
 if(!reader->read(&input,0,count,0,true,false)) return false;
 CarrierSynth synth; synth.setCurrentPlaybackSampleRate(rate); synth.setWaveform(waveform);
 juce::MidiBuffer midi;
 midi.addEvent(juce::MidiMessage::noteOn(1,45,(juce::uint8)110),0);
 midi.addEvent(juce::MidiMessage::noteOff(1,45),count-1);
 synth.renderNextBlock(carrier,midi,0,count);
 VocoderEngine engine; engine.prepare(rate); engine.setDryWet(1); engine.setOutputGain(1);
 engine.setAttackMs(10); engine.setReleaseMs(5); engine.setFormantShift(0);
 engine.setBandCount(56); engine.setFilterQ(7); engine.setSibilanceAmount(.10f);
 engine.setUnvoicedNoise(.10f); engine.setArticulationLift(1); engine.reset();
 for(int i=0;i<count;++i)
  output.setSample(0,i,engine.processSample(input.getSample(0,i),0.0f,false,carrier.getSample(0,i)));
 auto file=outputDir.getChildFile(speaker+"_carrier_"+profile+".wav");
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
  const Profile preEmphasisOff { "preemphasis_off",56,7,10,1,0,.10f,.10f,1 };
  const Profile preEmphasisOn { "preemphasis_on",56,7,10,1,0,.10f,.10f,1 };
  if(!render(input,dir,speaker,preEmphasisOff,0.0f))return 10;
  if(!render(input,dir,speaker,preEmphasisOn,0.15f))return 11;
  if(!renderCarrierComparison(input,dir,speaker,"rich",CarrierSynth::rich))return 6;
  if(!renderCarrierComparison(input,dir,speaker,"analog_stack",CarrierSynth::analogStack))return 7;
  if(!renderCarrierComparison(input,dir,speaker,"saw",CarrierSynth::saw))return 8;
  if(!renderCarrierComparison(input,dir,speaker,"square",CarrierSynth::square))return 9;
 }
 std::cout<<"Rendered "<<std::size(profiles)*2+12<<" candidate WAV files.\n"; return 0;
}
