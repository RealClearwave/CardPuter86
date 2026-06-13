#include "cardputer_speaker.h"
#include "hardware.h"
#include "gbConfig.h"
#include "gbGlobals.h"
#include <driver/i2s.h>
#include <Arduino.h>

// ===============================================
// I2S speaker driver for NS4168 amplifier
// BCLK = G41, LRCLK = G43, DATA = G42
// 16-bit mono, 16000 Hz sample rate
// ===============================================

#define I2S_PORT I2S_NUM_0

static bool i2s_initialized = false;

void cardputer_speaker_init(void) {
    i2s_config_t i2s_config = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 4,
        .dma_buf_len          = 128,
        .use_apll             = false,
        .tx_desc_auto_clear   = true,
        .fixed_mclk           = 0,
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num     = I2S_BCLK,
        .ws_io_num      = I2S_LRCLK,
        .data_out_num   = I2S_SDATA,
        .data_in_num    = I2S_PIN_NO_CHANGE,
    };

    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
#ifdef use_lib_log_serial
        Serial.printf("I2S driver install failed: %d\n", err);
#endif
        return;
    }

    err = i2s_set_pin(I2S_PORT, &pin_config);
    if (err != ESP_OK) {
#ifdef use_lib_log_serial
        Serial.printf("I2S set pin failed: %d\n", err);
#endif
        return;
    }

    // Zero the DMA buffer to avoid clicks
    i2s_zero_dma_buffer(I2S_PORT);

    i2s_initialized = true;

#ifdef use_lib_log_serial
    Serial.printf("I2S speaker initialized: %d Hz, pins BCLK=%d DATA=%d LRCLK=%d\n",
        SAMPLE_RATE, I2S_BCLK, I2S_SDATA, I2S_LRCLK);
#endif
}

void cardputer_speaker_write_sample(int16_t sample) {
    if (!i2s_initialized) return;

    size_t bytes_written = 0;
    // Write both left and right channels (same data for mono)
    int16_t samples[2] = { sample, sample };
    i2s_write(I2S_PORT, samples, sizeof(samples), &bytes_written, 0);
}

void cardputer_speaker_mute(void) {
    // Write zeros to flush
    for (int i = 0; i < 10; i++) {
        int16_t zero = 0;
        size_t written;
        i2s_write(I2S_PORT, &zero, sizeof(zero), &written, 0);
    }
}
