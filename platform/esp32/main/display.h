#pragma once
#include "vpet/framebuffer.h"

// ST7789 over SPI. The 120x120 canvas is drawn at 2x in the middle of the
// 240x280 panel, leaving 20px bars above and below.
void display_init();
// Waits for the previous frame's DMA transfer, then starts sending this one.
void display_present(const vpet::Framebuffer& fb);
