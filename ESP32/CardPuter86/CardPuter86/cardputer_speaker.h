#ifndef _CARDputer_SPEAKER_H
#define _CARDputer_SPEAKER_H

#include "gbConfig.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void cardputer_speaker_init(void);
void cardputer_speaker_write_sample(int16_t sample);
void cardputer_speaker_mute(void);

#ifdef __cplusplus
}
#endif

#endif
