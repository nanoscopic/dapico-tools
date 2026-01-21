#pragma once

#include <cstdint>

constexpr uint32_t FLASH_START = 0x10000000;
constexpr uint32_t FLASH_END_RP2040 = 0x11000000;
constexpr uint32_t FLASH_END_RP2350 = 0x14000000;

constexpr uint32_t SRAM_START = 0x20000000;
constexpr uint32_t SRAM_END_RP2040 = 0x20042000;
constexpr uint32_t SRAM_END_RP2350 = 0x20082000;
