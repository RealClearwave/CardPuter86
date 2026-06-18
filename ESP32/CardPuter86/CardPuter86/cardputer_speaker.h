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
void cardputer_speaker_self_test(void);
bool cardputer_speaker_task_running(void);
void cardputer_speaker_set_pc_speaker(uint8_t gate, uint8_t data,
                                      uint32_t divisor);

#ifdef __cplusplus
}
#endif

#endif
