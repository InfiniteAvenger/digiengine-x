// ESP32-S3 frontend for the Waveshare ESP32-S3-LCD-1.69.
#include <sys/time.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include "board.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rtc.h"
#include "vpet/game.h"

namespace {

const char* TAG = "vpet";
constexpr int64_t kFrameUs = 33'333;  // ~30 fps

class Esp32Platform : public vpet::Platform {
public:
    uint64_t now_ms() override {
        timeval tv;
        gettimeofday(&tv, nullptr);
        return uint64_t(tv.tv_sec) * 1000 + uint64_t(tv.tv_usec / 1000);
    }

    // Taken from the PC that built the firmware until there is a clock screen.
    int32_t utc_offset_s() override { return VPET_UTC_OFFSET_MIN * 60; }

    uint8_t buttons() override {
        uint8_t b = 0;
        if (!gpio_get_level(gpio_num_t(PIN_BTN_A))) b |= vpet::BTN_A;
        if (!gpio_get_level(gpio_num_t(PIN_BTN_B))) b |= vpet::BTN_B;
        if (!gpio_get_level(gpio_num_t(PIN_BTN_C))) b |= vpet::BTN_C;
        return b;
    }

    void present(const vpet::Framebuffer& fb) override { display_present(fb); }

    bool read_file(const std::string& path, std::string& out) override {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    }

    bool load_save(const std::string& slot, std::string& out) override { return read_file(path(slot), out); }

    bool write_save(const std::string& slot, const std::string& data) override {
        // Write then rename: littlefs renames atomically, so pulling the
        // battery mid-save leaves the old save intact.
        std::string tmp = path(slot) + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!(f << data)) return false;
        }
        return std::rename(tmp.c_str(), path(slot).c_str()) == 0;
    }

private:
    static std::string path(const std::string& slot) { return "/saves/" + slot + ".sav"; }
};

void hold_power_and_quiet_buzzer() {
    // On battery the board switches itself off unless SYS_EN is held high.
    gpio_reset_pin(gpio_num_t(PIN_SYS_EN));
    gpio_set_direction(gpio_num_t(PIN_SYS_EN), GPIO_MODE_OUTPUT);
    gpio_set_level(gpio_num_t(PIN_SYS_EN), 1);
    // Waveshare recommends keeping the buzzer pin low so it doesn't heat up.
    gpio_reset_pin(gpio_num_t(PIN_BUZZER));
    gpio_set_direction(gpio_num_t(PIN_BUZZER), GPIO_MODE_OUTPUT);
    gpio_set_level(gpio_num_t(PIN_BUZZER), 0);
}

void init_buttons() {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << PIN_BTN_A) | (1ULL << PIN_BTN_B) | (1ULL << PIN_BTN_C);
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;  // buttons connect the pin to GND
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

bool mount(const char* base_path, const char* label, bool format_if_needed) {
    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = base_path;
    conf.partition_label = label;
    conf.format_if_mount_failed = format_if_needed;
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) ESP_LOGE(TAG, "mounting %s failed: %s", label, esp_err_to_name(err));
    return err == ESP_OK;
}

void init_clock() {
    int64_t epoch = 0;
    bool valid = rtc_init() && rtc_read(epoch);
    if (!valid) {
        // First boot or the RTC lost power: fall back to the firmware's build time.
        epoch = VPET_BUILD_UTC;
        rtc_write(epoch);
        ESP_LOGW(TAG, "RTC not set; using build time");
    }
    timeval tv = {time_t(epoch), 0};
    settimeofday(&tv, nullptr);
    ESP_LOGI(TAG, "clock: %lld UTC, offset %d min", (long long)epoch, VPET_UTC_OFFSET_MIN);
}

// How long one simulated day of catch-up takes on this chip.
void benchmark(vpet::Game& game) {
    vpet::Pet copy = game.pet();
    int64_t t0 = esp_timer_get_time();
    vpet::simulate(game.pack(), copy, copy.sim_ms + 24ull * 3600 * 1000, VPET_UTC_OFFSET_MIN * 60);
    int64_t us = esp_timer_get_time() - t0;
    ESP_LOGI(TAG, "catch-up: 1 day = %lld ms (30-day cap = %lld ms)", (long long)(us / 1000),
             (long long)(us * 30 / 1000));
}

}  // namespace

extern "C" void app_main() {
    hold_power_and_quiet_buzzer();
    init_buttons();
    display_init();
    init_clock();
    mount("/packs", "packs", false);
    mount("/saves", "saves", true);

    // Heap, not stack: the game holds a 28 KB framebuffer.
    auto* platform = new Esp32Platform();
    auto* game = new vpet::Game(*platform);
    if (game->init("/packs")) benchmark(*game);
    else ESP_LOGE(TAG, "pack failed to load; error shown on screen");

    int64_t worst_us = 0, next_report = esp_timer_get_time() + 10'000'000;
    while (true) {
        int64_t start = esp_timer_get_time();
        game->frame();
        int64_t took = esp_timer_get_time() - start;
        if (took > worst_us) worst_us = took;
        if (start >= next_report) {
            ESP_LOGI(TAG, "slowest frame in last 10 s: %lld us, free heap %u", (long long)worst_us,
                     unsigned(esp_get_free_heap_size()));
            worst_us = 0;
            next_report = start + 10'000'000;
        }
        int64_t wait_ms = (kFrameUs - took) / 1000;
        vTaskDelay(pdMS_TO_TICKS(wait_ms > 1 ? wait_ms : 1));
    }
}
