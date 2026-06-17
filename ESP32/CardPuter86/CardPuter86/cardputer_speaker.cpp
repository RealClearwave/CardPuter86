#include "cardputer_speaker.h"
#include "hardware.h"
#include "gbConfig.h"
#include "gbGlobals.h"
#include <driver/i2s.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ===============================================
// I2S speaker driver for NS4168 amplifier
// BCLK = G41, LRCLK = G43, DATA = G42
// 16-bit mono, 16000 Hz sample rate
// ===============================================

#define I2S_PORT I2S_NUM_0
#define AUDIO_TASK_CORE 0
#define AUDIO_TASK_STACK 4096
#define AUDIO_TASK_PRIORITY 3
#define AUDIO_BUFFER_SAMPLES 128

static bool i2s_initialized = false;
static TaskHandle_t audio_task_handle = nullptr;

static int16_t speaker_sample_from_phase(uint32_t *phase) {
    const int volume = gb_silence ? 0 : gb_volumen01;
    const int frequency = gb_frecuencia01;
    if (volume <= 0 || frequency <= 0) {
        *phase = 0;
        return 0;
    }

    const uint32_t step =
        (uint32_t)(((uint64_t)frequency << 32) / SAMPLE_RATE);
    *phase += step;
    const int amplitude = volume * 256;
    return (*phase & 0x80000000UL) ? amplitude : -amplitude;
}

static void audio_task(void *parameter) {
    (void)parameter;
    int16_t samples[AUDIO_BUFFER_SAMPLES];
    uint32_t phase = 0;

    while (true) {
        for (size_t i = 0; i < AUDIO_BUFFER_SAMPLES; i++) {
            samples[i] = speaker_sample_from_phase(&phase);
        }

        size_t bytes_written = 0;
        i2s_write(I2S_PORT, samples, sizeof(samples), &bytes_written,
                  portMAX_DELAY);
    }
}

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

    BaseType_t created = xTaskCreatePinnedToCore(
        audio_task, "audio", AUDIO_TASK_STACK, nullptr, AUDIO_TASK_PRIORITY,
        &audio_task_handle, AUDIO_TASK_CORE);
    if (created != pdPASS) {
        audio_task_handle = nullptr;
#ifdef use_lib_log_serial
        Serial.println("I2S audio task creation failed");
#endif
    }

#ifdef use_lib_log_serial
    Serial.printf("I2S speaker initialized: %d Hz, pins BCLK=%d DATA=%d LRCLK=%d\n",
        SAMPLE_RATE, I2S_BCLK, I2S_SDATA, I2S_LRCLK);
#endif
}

void cardputer_speaker_write_sample(int16_t sample) {
    if (!i2s_initialized) return;

    size_t bytes_written = 0;
    i2s_write(I2S_PORT, &sample, sizeof(sample), &bytes_written, 0);
}

void cardputer_speaker_mute(void) {
    // Write zeros to flush
    for (int i = 0; i < 10; i++) {
        int16_t zero = 0;
        size_t written;
        i2s_write(I2S_PORT, &zero, sizeof(zero), &written, 0);
    }
}

bool cardputer_speaker_task_running(void) {
    return audio_task_handle != nullptr;
}
