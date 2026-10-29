#include "display.h"
#include <cstring>
#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {

constexpr int kScale = 2;
constexpr int kOutW = vpet::kScreenW * kScale;  // 240
constexpr int kOutH = vpet::kScreenH * kScale;  // 240
constexpr int kOutY = (LCD_H - kOutH) / 2;      // 20

esp_lcd_panel_handle_t panel = nullptr;
SemaphoreHandle_t transfer_done = nullptr;
uint16_t* out = nullptr;  // kOutW x kOutH, big-endian RGB565, DMA-capable

bool on_transfer_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* ctx) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(ctx), &woken);
    return woken == pdTRUE;
}

void draw(int y, const uint16_t* pixels) {
    xSemaphoreTake(transfer_done, portMAX_DELAY);
    esp_lcd_panel_draw_bitmap(panel, 0, y, kOutW, y + kOutH, pixels);
}

}  // namespace

void display_init() {
    transfer_done = xSemaphoreCreateBinary();
    xSemaphoreGive(transfer_done);
    out = static_cast<uint16_t*>(heap_caps_calloc(kOutW * kOutH, sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));

    spi_bus_config_t bus = {};
    bus.sclk_io_num = PIN_LCD_SCK;
    bus.mosi_io_num = PIN_LCD_MOSI;
    bus.miso_io_num = -1;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = kOutW * 40 * sizeof(uint16_t);  // esp_lcd splits frames into chunks this size
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.dc_gpio_num = gpio_num_t(PIN_LCD_DC);
    io_cfg.cs_gpio_num = gpio_num_t(PIN_LCD_CS);
    io_cfg.pclk_hz = 40 * 1000 * 1000;
    io_cfg.lcd_cmd_bits = 8;
    io_cfg.lcd_param_bits = 8;
    io_cfg.spi_mode = 0;
    io_cfg.trans_queue_depth = 10;
    io_cfg.on_color_trans_done = on_transfer_done;
    io_cfg.user_ctx = transfer_done;
    esp_lcd_panel_io_handle_t io = nullptr;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t(SPI2_HOST), &io_cfg, &io));

    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = gpio_num_t(PIN_LCD_RST);
    panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_cfg.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));  // IPS panel
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, 0, LCD_GAP_Y));

    // Black out the whole panel (two overlapping 240-row blits cover 280 rows).
    draw(0, out);
    draw(LCD_H - kOutH, out);
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    gpio_reset_pin(gpio_num_t(PIN_LCD_BL));
    gpio_set_direction(gpio_num_t(PIN_LCD_BL), GPIO_MODE_OUTPUT);
    gpio_set_level(gpio_num_t(PIN_LCD_BL), 1);
}

void display_present(const vpet::Framebuffer& fb) {
    xSemaphoreTake(transfer_done, portMAX_DELAY);  // previous frame still sending
    const vpet::Color* src = fb.pixels();
    for (int y = 0; y < vpet::kScreenH; ++y) {
        uint16_t* row = out + y * kScale * kOutW;
        for (int x = 0; x < vpet::kScreenW; ++x) {
            uint16_t c = src[y * vpet::kScreenW + x];
            c = uint16_t((c >> 8) | (c << 8));  // the panel wants big-endian
            row[x * 2] = row[x * 2 + 1] = c;
        }
        std::memcpy(row + kOutW, row, kOutW * sizeof(uint16_t));
    }
    esp_lcd_panel_draw_bitmap(panel, 0, kOutY, kOutW, kOutY + kOutH, out);
}
