#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "muse_pmu.h"
static uint8_t registers[256];
static bool fail_read, fail_write, corrupt_timing;
static int writes;
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *cfg, i2c_master_dev_handle_t *out) {
    (void)bus; (void)cfg; *out = (void *)1; return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t *tx, size_t tn, uint8_t *rx, size_t rn, int ms) {
    (void)dev; (void)tn; (void)rn; (void)ms;
    if (fail_read) return ESP_FAIL;
    *rx = registers[*tx]; return ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *tx, size_t n, int ms) {
    (void)dev; (void)n; (void)ms;
    writes++;
    if (fail_write) return ESP_FAIL;
    if (tx[0] == 0x49) registers[0x49] &= ~tx[1];
    else if (tx[0] == 0x27 && corrupt_timing) registers[0x27] = tx[1] ^ 0x10;
    else registers[tx[0]] = tx[1];
    return ESP_OK;
}
int main(void) {
    unsigned event = 99;
    assert(muse_pmu_poll_key_checked(&event) == ESP_ERR_INVALID_STATE && event == 0);
    registers[0x27] = 0x03; /* ONLEVEL=2s must survive both setup calls */
    assert(muse_pmu_init((void *)1, true) == ESP_OK);
    assert(registers[0x27] == 0x0f);
    assert(muse_pmu_key_long_1500ms() == ESP_OK);
    assert(registers[0x27] == 0x1f); /* 1.5s IRQ + 10s off + unchanged on */
    corrupt_timing = true;
    assert(muse_pmu_key_long_1500ms() == ESP_ERR_INVALID_STATE);
    corrupt_timing = false;
    fail_read = true; writes = 0;
    assert(muse_pmu_poll_key_checked(&event) == ESP_FAIL && event == 0 && writes == 0);
    fail_read = false;
    registers[0x49] = 0x8f; /* VBUS event must not be acknowledged */
    fail_write = true;
    assert(muse_pmu_poll_key_checked(&event) == ESP_FAIL && event == 0);
    assert(registers[0x49] == 0x8f);
    fail_write = false;
    assert(muse_pmu_poll_key_checked(&event) == ESP_OK && event == 15);
    assert(registers[0x49] == 0x80);
    writes = 0;
    assert(muse_pmu_poll_key_checked(&event) == ESP_OK && event == 0 && writes == 0);
    registers[0x49] = 0x0a;
    assert(muse_pmu_poll_key() == (MUSE_PMU_KEY_PRESS | MUSE_PMU_KEY_CLICK));
    puts("PMU: actual I2C API, read/ack failures, timing readback and legacy poll passed");
}
