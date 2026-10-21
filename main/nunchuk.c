// nunchuk.c - Wii Nunchuk on I2C (address 0x52), same wiring as Galagino's
// CYD configuration: SDA = GPIO22, SCL = GPIO27, powered from 3.3 V.
//
// Uses the "unencrypted" init (0xF0=0x55, 0xFB=0x00) that works with original
// and third-party controllers. The controller can be plugged in at any time:
// while it is missing, a reconnection is tried every 500 ms.
#include "nunchuk.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#define NUNCHUK_ADDR 0x52
static const char *TAG = "nunchuk";
static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t dev;
static bool connected;
static int64_t next_try;

static bool nc_handshake(void)
{
    if (!bus) return false;
    if (i2c_master_probe(bus, NUNCHUK_ADDR, 20) != ESP_OK) return false;
    if (!dev) {
        i2c_device_config_t dc = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = NUNCHUK_ADDR,
            .scl_speed_hz = 100000,
        };
        if (i2c_master_bus_add_device(bus, &dc, &dev) != ESP_OK) { dev = NULL; return false; }
    }
    const uint8_t i1[2] = { 0xF0, 0x55 }, i2[2] = { 0xFB, 0x00 };
    if (i2c_master_transmit(dev, i1, 2, 20) != ESP_OK) return false;
    esp_rom_delay_us(1000);
    if (i2c_master_transmit(dev, i2, 2, 20) != ESP_OK) return false;
    esp_rom_delay_us(1000);
    return true;
}

bool nunchuk_init(int sda, int scl)
{
    i2c_master_bus_config_t bc = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bc, &bus) != ESP_OK) {
        ESP_LOGW(TAG, "I2C bus init failed");
        bus = NULL;
        return false;
    }
    connected = nc_handshake();
    ESP_LOGI(TAG, "Nunchuk %s (SDA %d, SCL %d)", connected ? "detected" : "not detected", sda, scl);
    next_try = esp_timer_get_time() + 500000;
    return connected;
}

bool nunchuk_read(nunchuk_t *n)
{
    n->ok = false; n->c = n->z = false; n->jx = n->jy = 128;
    if (!bus) return false;
    int64_t now = esp_timer_get_time();
    if (!connected) {
        if (now < next_try) return false;
        next_try = now + 500000;
        connected = nc_handshake();
        if (connected) ESP_LOGI(TAG, "Nunchuk connected");
        if (!connected) return false;
    }
    uint8_t reg = 0x00, d[6];
    if (i2c_master_transmit(dev, &reg, 1, 10) != ESP_OK) goto lost;
    esp_rom_delay_us(200);                      // conversion delay
    if (i2c_master_receive(dev, d, 6, 10) != ESP_OK) goto lost;
    if (d[0] == 0xFF && d[1] == 0xFF && d[5] == 0xFF) goto lost;   // not initialised / unplugged
    n->jx = d[0];
    n->jy = d[1];
    n->z = !(d[5] & 0x01);
    n->c = !(d[5] & 0x02);
    n->ok = true;
    return true;
lost:
    ESP_LOGW(TAG, "Nunchuk lost");
    connected = false;
    i2c_master_bus_reset(bus);
    next_try = now + 500000;
    return false;
}

bool nunchuk_connected(void) { return connected; }
