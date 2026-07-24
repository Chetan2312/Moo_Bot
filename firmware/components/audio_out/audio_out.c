#include "audio_out.h"

#include <math.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "audio_out";

#define SAMPLE_RATE_HZ  22050
#define CHUNK_SAMPLES   256

static i2s_chan_handle_t s_tx;
static float s_volume = 1.0f;

void audio_out_set_volume(float volume)
{
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    s_volume = volume;
}

esp_err_t audio_out_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;   /* zero DMA bufs after send, else last chunk loops forever */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, NULL), TAG, "chan create");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_MOO_PIN_I2S_AMP_BCLK,
            .ws = CONFIG_MOO_PIN_I2S_AMP_LRCLK,
            .dout = CONFIG_MOO_PIN_I2S_AMP_DIN,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std_cfg), TAG, "std init");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "chan enable");

    ESP_LOGI(TAG, "MAX98357A up: bclk=%d lrclk=%d din=%d @ %d Hz",
             CONFIG_MOO_PIN_I2S_AMP_BCLK, CONFIG_MOO_PIN_I2S_AMP_LRCLK,
             CONFIG_MOO_PIN_I2S_AMP_DIN, SAMPLE_RATE_HZ);
    return ESP_OK;
}

esp_err_t audio_out_tone(float f0_hz, float f1_hz, uint32_t duration_ms, float amplitude)
{
    ESP_RETURN_ON_FALSE(s_tx, ESP_ERR_INVALID_STATE, TAG, "not init");
    if (amplitude < 0.0f) amplitude = 0.0f;
    if (amplitude > 1.0f) amplitude = 1.0f;
    amplitude *= s_volume;

    const uint32_t total = (uint32_t)((uint64_t)SAMPLE_RATE_HZ * duration_ms / 1000);
    int16_t chunk[CHUNK_SAMPLES];
    float phase = 0.0f;

    for (uint32_t done = 0; done < total; ) {
        const uint32_t n = (total - done) < CHUNK_SAMPLES ? (total - done) : CHUNK_SAMPLES;
        for (uint32_t i = 0; i < n; i++) {
            const float t = (float)(done + i) / (float)total;
            const float f = f0_hz + (f1_hz - f0_hz) * t;
            phase += 2.0f * (float)M_PI * f / (float)SAMPLE_RATE_HZ;
            /* short attack/release ramp kills click at edges */
            float env = 1.0f;
            const uint32_t k = done + i;
            const uint32_t ramp = SAMPLE_RATE_HZ / 200;  /* 5 ms */
            if (k < ramp) env = (float)k / ramp;
            else if (total - k < ramp) env = (float)(total - k) / ramp;
            chunk[i] = (int16_t)(sinf(phase) * amplitude * env * 32767.0f);
        }
        size_t written = 0;
        ESP_RETURN_ON_ERROR(i2s_channel_write(s_tx, chunk, n * sizeof(int16_t),
                                              &written, portMAX_DELAY),
                            TAG, "i2s write");
        done += n;
    }
    return ESP_OK;
}

esp_err_t audio_out_pat_sound(void)
{
    ESP_RETURN_ON_ERROR(audio_out_tone(700.0f, 1400.0f, 110, 0.8f), TAG, "chirp1");
    vTaskDelay(pdMS_TO_TICKS(35));
    return audio_out_tone(1000.0f, 1900.0f, 90, 0.8f);
}

esp_err_t audio_out_alert_sound(void)
{
    ESP_RETURN_ON_ERROR(audio_out_tone(1600.0f, 1000.0f, 150, 0.8f), TAG, "alert1");
    vTaskDelay(pdMS_TO_TICKS(70));
    return audio_out_tone(1600.0f, 1000.0f, 150, 0.8f);
}
