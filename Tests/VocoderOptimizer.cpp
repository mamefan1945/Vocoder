#include "VocoderEngine.h"
#include "CarrierSynth.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

struct Profile { const char* name; int bands; float q, attack, release, formant, sibilance, unvoiced, articulation; };
static const Profile profiles[] = {
 {"r05_fm2",56,7,10,5,-2,.10f,.10f,1},
 {"r05_fm1p5",56,7,10,5,-1.5,.10f,.10f,1},
 {"r05_fm1",56,7,10,5,-1,.10f,.10f,1},
 {"r05_fm0p5",56,7,10,5,-0.5,.10f,.10f,1},
 {"r05_f0",56,7,10,5,0,.10f,.10f,1},
 {"r06_fm2",56,7,10,6,-2,.10f,.10f,1},
 {"r06_fm1p5",56,7,10,6,-1.5,.10f,.10f,1},
 {"r06_fm1",56,7,10,6,-1,.10f,.10f,1},
 {"r06_fm0p5",56,7,10,6,-0.5,.10f,.10f,1},
 {"r06_f0",56,7,10,6,0,.10f,.10f,1},
 {"r07_fm2",56,7,10,7,-2,.10f,.10f,1},
 {"r07_fm1p5",56,7,10,7,-1.5,.10f,.10f,1},
 {"r07_fm1",56,7,10,7,-1,.10f,.10f,1},
 {"r07_fm0p5",56,7,10,7,-0.5,.10f,.10f,1},
 {"r07_f0",56,7,10,7,0,.10f,.10f,1},
 {"r08_fm2",56,7,10,8,-2,.10f,.10f,1},
 {"r08_fm1p5",56,7,10,8,-1.5,.10f,.10f,1},
 {"r08_fm1",56,7,10,8,-1,.10f,.10f,1},
 {"r08_fm0p5",56,7,10,8,-0.5,.10f,.10f,1},
 {"r08_f0",56,7,10,8,0,.10f,.10f,1},
 {"r09_fm2",56,7,10,9,-2,.10f,.10f,1},
 {"r09_fm1p5",56,7,10,9,-1.5,.10f,.10f,1},
 {"r09_fm1",56,7,10,9,-1,.10f,.10f,1},
 {"r09_fm0p5",56,7,10,9,-0.5,.10f,.10f,1},
 {"r09_f0",56,7,10,9,0,.10f,.10f,1},
 {"r10_fm2",56,7,10,10,-2,.10f,.10f,1},
 {"r10_fm1p5",56,7,10,10,-1.5,.10f,.10f,1},
 {"r10_fm1",56,7,10,10,-1,.10f,.10f,1},
 {"r10_fm0p5",56,7,10,10,-0.5,.10f,.10f,1},
 {"r10_f0",56,7,10,10,0,.10f,.10f,1},
 // Wider, one-variable-at-a-time diagnostics for short consonants.
 {"wide_r01",56,7,10,1,-2,.10f,.10f,1},
 {"wide_r20",56,7,10,20,-2,.10f,.10f,1},
 {"wide_r50",56,7,10,50,-2,.10f,.10f,1},
 {"attack_a01",56,7,1,7,-2,.10f,.10f,1},
 {"attack_a30",56,7,30,7,-2,.10f,.10f,1},
 {"articulation_0",56,7,10,7,-2,.10f,.10f,0},
 {"articulation_2",56,7,10,7,-2,.10f,.10f,2},
 {"unvoiced_0",56,7,10,7,-2,.10f,0,1},
 {"unvoiced_2",56,7,10,7,-2,.10f,2.0f,1}
};

bool render(const juce::File& inputFile, const juce::File& outputDir, const juce::String& speaker, const Profile& p,
            float preEmphasis = 0.15f, float plosiveBurst = 1.0f) {
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
 engine.setPreEmphasisAmount(preEmphasis); engine.setPlosiveBurst(plosiveBurst); engine.reset();
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
  const Profile preEmphasisOff { "preemphasis_off",56,7,10,7,-2,.10f,.10f,1 };
  const Profile preEmphasisOn { "preemphasis_on",56,7,10,7,-2,.10f,.10f,1 };
  if(!render(input,dir,speaker,preEmphasisOff,0.0f))return 10;
  if(!render(input,dir,speaker,preEmphasisOn,0.15f))return 11;
  const Profile plosiveOff { "plosive_off",56,7,10,7,-2,.10f,.10f,1 };
  const Profile plosiveOn { "plosive_on",56,7,10,7,-2,.10f,.10f,1 };
  if(!render(input,dir,speaker,plosiveOff,0.15f,0.0f))return 12;
  if(!render(input,dir,speaker,plosiveOn,0.15f,1.0f))return 13;
  if(!renderCarrierComparison(input,dir,speaker,"rich",CarrierSynth::rich))return 6;
  if(!renderCarrierComparison(input,dir,speaker,"analog_stack",CarrierSynth::analogStack))return 7;
  if(!renderCarrierComparison(input,dir,speaker,"saw",CarrierSynth::saw))return 8;
  if(!renderCarrierComparison(input,dir,speaker,"square",CarrierSynth::square))return 9;
 }
 std::cout<<"Rendered "<<std::size(profiles)*2+12<<" candidate WAV files.\n"; return 0;
}
