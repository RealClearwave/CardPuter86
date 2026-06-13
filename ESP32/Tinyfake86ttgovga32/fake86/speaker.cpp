// speaker.cpp — Cardputer I2S speaker emulation
// Generates PC speaker square waves via I2S DMA

#include "config.h"
#include <stdint.h>
#include "i8253.h"
#include "gbConfig.h"
#include "cardputer_speaker.h"

extern struct i8253_s i8253;

extern uint64_t gensamplerate;
uint64_t speakerfullstep, speakerhalfstep, speakercurstep = 0;
int16_t speakerpos = 0;

int16_t speakergensample() {
    int16_t speakervalue;

    speakerfullstep = (uint64_t)((float)gensamplerate / (float)i8253.chanfreq[2]);
    if (speakerfullstep < 2) speakerfullstep = 2;
    speakerhalfstep = speakerfullstep >> 1;
    if (speakercurstep < speakerhalfstep) {
        speakervalue = 32;
    } else {
        speakervalue = -32;
    }
    speakercurstep = (speakercurstep + 1) % speakerfullstep;
    return (speakervalue);
}
