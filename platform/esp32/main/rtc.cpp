#include "rtc.h"
#include "board.h"
#include "driver/i2c_master.h"

namespace {

constexpr uint8_t kRegSeconds = 0x04;  // seconds..years are 7 BCD registers from here
constexpr int kTimeoutMs = 50;

i2c_master_dev_handle_t dev = nullptr;

uint8_t from_bcd(uint8_t v) { return uint8_t((v >> 4) * 10 + (v & 0x0F)); }
uint8_t to_bcd(int v) { return uint8_t(((v / 10) << 4) | (v % 10)); }

// Howard Hinnant's days-from-civil; avoids relying on timegm().
int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void civil_from_days(int64_t z, int& y, int& m, int& d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    d = int(doy - (153 * mp + 2) / 5 + 1);
    m = int(mp < 10 ? mp + 3 : mp - 9);
    y = int(yoe + era * 400 + (m <= 2));
}

}  // namespace

bool rtc_init() {
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = I2C_NUM_0;
    bus_cfg.sda_io_num = gpio_num_t(PIN_I2C_SDA);
    bus_cfg.scl_io_num = gpio_num_t(PIN_I2C_SCL);
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t bus = nullptr;
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) return false;

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = RTC_I2C_ADDR;
    dev_cfg.scl_speed_hz = 400000;
    return i2c_master_bus_add_device(bus, &dev_cfg, &dev) == ESP_OK;
}

bool rtc_read(int64_t& epoch_s) {
    if (!dev) return false;
    uint8_t reg = kRegSeconds, r[7];
    if (i2c_master_transmit_receive(dev, &reg, 1, r, sizeof r, kTimeoutMs) != ESP_OK) return false;
    if (r[0] & 0x80) return false;  // oscillator stopped: time is not valid
    int sec = from_bcd(r[0] & 0x7F), min = from_bcd(r[1] & 0x7F), hour = from_bcd(r[2] & 0x3F);
    int day = from_bcd(r[3] & 0x3F), month = from_bcd(r[5] & 0x1F), year = 2000 + from_bcd(r[6]);
    epoch_s = days_from_civil(year, month, day) * 86400 + hour * 3600 + min * 60 + sec;
    return year >= 2024;
}

bool rtc_write(int64_t epoch_s) {
    if (!dev) return false;
    int64_t days = epoch_s / 86400, rem = epoch_s % 86400;
    int y, m, d;
    civil_from_days(days, y, m, d);
    int weekday = int((days + 4) % 7);  // 1970-01-01 was a Thursday
    uint8_t buf[8] = {kRegSeconds,
                      to_bcd(int(rem % 60)),  // also clears the oscillator-stop flag
                      to_bcd(int(rem / 60 % 60)),
                      to_bcd(int(rem / 3600)),
                      to_bcd(d),
                      uint8_t(weekday),
                      to_bcd(m),
                      to_bcd(y - 2000)};
    return i2c_master_transmit(dev, buf, sizeof buf, kTimeoutMs) == ESP_OK;
}
