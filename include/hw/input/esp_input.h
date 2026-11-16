/*
 * ESP polled keyboard and pointer input device
 *
 * A small MMIO device that exposes a USB HID keyboard and an absolute
 * pointer (tablet) to the guest. The host window's keyboard and mouse events
 * are queued by QEMU's HID code (hw/input/hid.c); the guest polls the device
 * instead of taking interrupts.
 *
 * Registers (32-bit accesses only, other sizes read 0 and are ignored):
 *   0x00 ID      read-only, ESP_INPUT_ID_VALUE
 *   0x04 VERSION read-only, ESP_INPUT_VERSION_VALUE
 *   0x08 STATUS  bit 0: keyboard has events, bit 1: pointer has events
 *   0x0c POP     write-only: bit 0 latches the next keyboard report into
 *                KBD0/KBD1, bit 1 latches the next pointer report into
 *                PTR0/PTR1. Reads return 0, so a debugger read has no effect.
 *   0x10 KBD0    keyboard report bytes 0..3, little-endian
 *                (modifiers, 0, key usage 1, key usage 2)
 *   0x14 KBD1    keyboard report bytes 4..7 (key usages 3 to 6)
 *   0x18 PTR0    pointer report bytes 0..3, little-endian
 *                (buttons, x low, x high, y low)
 *   0x1c PTR1    pointer report bytes 4..5 (y high, wheel; wheel up is +1)
 *
 * Pointer x and y are absolute, 0..0x7fff.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#pragma once

#include "hw/sysbus.h"
#include "hw/input/hid.h"
#include "hw/registerfields.h"
#include "qom/object.h"

#define TYPE_ESP_INPUT "input.esp.hid"
OBJECT_DECLARE_SIMPLE_TYPE(ESPInputState, ESP_INPUT)

#define ESP_INPUT_ID_VALUE      0x51494E50
#define ESP_INPUT_VERSION_VALUE 1

REG32(INPUT_ID, 0x00)
REG32(INPUT_VERSION, 0x04)
REG32(INPUT_STATUS, 0x08)
    FIELD(INPUT_STATUS, KBD, 0, 1)
    FIELD(INPUT_STATUS, PTR, 1, 1)
REG32(INPUT_POP, 0x0c)
    FIELD(INPUT_POP, KBD, 0, 1)
    FIELD(INPUT_POP, PTR, 1, 1)
REG32(INPUT_KBD0, 0x10)
REG32(INPUT_KBD1, 0x14)
REG32(INPUT_PTR0, 0x18)
REG32(INPUT_PTR1, 0x1c)

#define ESP_INPUT_IO_SIZE 0x20

struct ESPInputState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    HIDState kbd;
    HIDState ptr;
    uint8_t kbd_latch[8];
    uint8_t ptr_latch[8];
};
