#pragma once
#include <cstdint>

// PCF85063 real-time clock, kept in UTC. The backup battery keeps it running
// while the board is unplugged.
bool rtc_init();
// Seconds since the Unix epoch, or false if the clock was never set or lost
// power (oscillator-stop flag).
bool rtc_read(int64_t& epoch_s);
bool rtc_write(int64_t epoch_s);
