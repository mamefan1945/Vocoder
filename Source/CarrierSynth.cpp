#include "CarrierSynth.h"

CarrierSynth::CarrierSynth()
{
    addSound(new CarrierSound());
    for (int i = 0; i < 16; ++i)
        addVoice(new CarrierVoice(waveform, octave));
}
