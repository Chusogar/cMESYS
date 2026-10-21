// dac_audio.c - non-blocking-DMA audio output on the ESP32 built-in DAC.
//
// The DMA runs as a cyclic chain (dac_continuous async mode). Each time a
// buffer has been played, the ISR refills it from a software ring buffer
// (silence if the ring is empty), so the DMA can never stall. The producer
// (emulator) blocks in dac_audio_push() while the ring is full, which paces
// emulation to the DAC clock. If the DAC ever stops consuming, push() falls
// back to timer pacing so the game keeps running.
#include "dac_audio.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/dac_continuous.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_attr.h"

#define RING        2048            // samples, power of two
#define DMA_BUF     1024            // bytes per DMA buffer (= 512 samples, 16-bit slots)
#define DMA_DESC    4

static const char *TAG = "dac_audio";
static dac_continuous_handle_t dac;
static uint8_t ring[RING];
static volatile uint32_t r_head, r_tail;       // head: producer, tail: ISR
static uint8_t last = 128;
static SemaphoreHandle_t space;
static uint32_t rate_hz;
static volatile uint32_t isr_count;
static bool dead;
static int64_t t_next;

static bool IRAM_ATTR on_convert_done(dac_continuous_handle_t h, const dac_event_data_t *e, void *user)
{
    static uint8_t tmp[DMA_BUF / 2];
    size_t n = e->buf_size / 2;
    if (n > sizeof tmp) n = sizeof tmp;
    uint32_t t = r_tail, avail = r_head - t;
    for (size_t i = 0; i < n; i++) {
        if (avail) { last = ring[t & (RING - 1)]; t++; avail--; }
        tmp[i] = last;                            // underrun: hold last value (no click)
    }
    r_tail = t;
    dac_continuous_write_asynchronously(h, (uint8_t *)e->buf, e->buf_size, tmp, n, NULL);
    isr_count++;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(space, &hp);
    return hp == pdTRUE;
}

bool dac_audio_start(uint32_t rate)
{
    rate_hz = rate;
    space = xSemaphoreCreateBinary();
    dac_continuous_config_t cfg = {
        .chan_mask = DAC_CHANNEL_MASK_CH1,      // GPIO26 -> speaker amplifier
        .desc_num = DMA_DESC,
        .buf_size = DMA_BUF,
        .freq_hz = rate,
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_DEFAULT,
        .chan_mode = DAC_CHANNEL_MODE_SIMUL,
    };
    dac_event_callbacks_t cbs = { .on_convert_done = on_convert_done, .on_stop = NULL };
    if (dac_continuous_new_channels(&cfg, &dac) != ESP_OK ||
        dac_continuous_register_event_callback(dac, &cbs, NULL) != ESP_OK ||
        dac_continuous_enable(dac) != ESP_OK ||
        dac_continuous_start_async_writing(dac) != ESP_OK) {
        ESP_LOGW(TAG, "DAC init failed, running without sound (timer pacing)");
        dead = true;
        return false;
    }
    ESP_LOGI(TAG, "DAC async output at %u Hz", (unsigned)rate);
    return true;
}

static void timer_pace(int n)
{
    int64_t now = esp_timer_get_time();
    if (!t_next || t_next < now - 100000) t_next = now;
    t_next += (int64_t)n * 1000000 / (rate_hz ? rate_hz : 44100);
    while (esp_timer_get_time() < t_next) vTaskDelay(1);
}

void dac_audio_push(const uint8_t *s, int n)
{
    if (dead || !space) { timer_pace(n); return; }
    while (n > 0) {
        uint32_t used = r_head - r_tail;
        uint32_t room = RING - used;
        if (room == 0) {
            uint32_t c0 = isr_count;
            if (xSemaphoreTake(space, pdMS_TO_TICKS(200)) != pdTRUE && isr_count == c0) {
                ESP_LOGE(TAG, "DAC stopped consuming; switching to timer pacing");
                dead = true;
                timer_pace(n);
                return;
            }
            continue;
        }
        int k = (int)room < n ? (int)room : n;
        uint32_t h = r_head;
        for (int i = 0; i < k; i++) ring[(h + i) & (RING - 1)] = s[i];
        __sync_synchronize();
        r_head = h + (uint32_t)k;
        s += k; n -= k;
    }
}

uint32_t dac_audio_queued(void) { return r_head - r_tail; }
bool dac_audio_ok(void) { return !dead; }
