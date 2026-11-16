/*
 * ESP polled keyboard and pointer input device
 *
 * See include/hw/input/esp_input.h for the register map.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/input/esp_input.h"

static void esp_input_event(HIDState *hs)
{
    /* The guest polls STATUS, there is nothing to notify */
}

static uint64_t esp_input_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESPInputState *s = ESP_INPUT(opaque);
    uint32_t r = 0;

    if (size != sizeof(uint32_t)) {
        return 0;
    }

    switch (addr) {
    case A_INPUT_ID:
        r = ESP_INPUT_ID_VALUE;
        break;
    case A_INPUT_VERSION:
        r = ESP_INPUT_VERSION_VALUE;
        break;
    case A_INPUT_STATUS:
        r = FIELD_DP32(r, INPUT_STATUS, KBD, hid_has_events(&s->kbd));
        r = FIELD_DP32(r, INPUT_STATUS, PTR, hid_has_events(&s->ptr));
        break;
    case A_INPUT_KBD0:
        r = ldl_le_p(s->kbd_latch);
        break;
    case A_INPUT_KBD1:
        r = ldl_le_p(s->kbd_latch + 4);
        break;
    case A_INPUT_PTR0:
        r = ldl_le_p(s->ptr_latch);
        break;
    case A_INPUT_PTR1:
        r = lduw_le_p(s->ptr_latch + 4);
        break;
    default:
        break;
    }

    return r;
}

static void esp_input_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned int size)
{
    ESPInputState *s = ESP_INPUT(opaque);

    if (size != sizeof(uint32_t) || addr != A_INPUT_POP) {
        return;
    }

    if (FIELD_EX32(value, INPUT_POP, KBD)) {
        hid_keyboard_poll(&s->kbd, s->kbd_latch, sizeof(s->kbd_latch));
    }
    if (FIELD_EX32(value, INPUT_POP, PTR)) {
        /* A report is 6 bytes, the last two bytes of the latch stay 0 */
        hid_pointer_poll(&s->ptr, s->ptr_latch, 6);
    }
}

static const MemoryRegionOps esp_input_ops = {
    .read = esp_input_read,
    .write = esp_input_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp_input_reset_hold(Object *obj, ResetType type)
{
    ESPInputState *s = ESP_INPUT(obj);

    hid_reset(&s->kbd);
    hid_reset(&s->ptr);
    memset(s->kbd_latch, 0, sizeof(s->kbd_latch));
    memset(s->ptr_latch, 0, sizeof(s->ptr_latch));
}

static void esp_input_init(Object *obj)
{
    ESPInputState *s = ESP_INPUT(obj);

    memory_region_init_io(&s->iomem, obj, &esp_input_ops, s,
                          TYPE_ESP_INPUT, ESP_INPUT_IO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void esp_input_realize(DeviceState *dev, Error **errp)
{
    ESPInputState *s = ESP_INPUT(dev);

    /* A tablet is absolute: the guest gets positions inside the window */
    hid_init(&s->kbd, HID_KEYBOARD, esp_input_event);
    hid_init(&s->ptr, HID_TABLET, esp_input_event);
}

static void esp_input_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp_input_reset_hold;
    dc->realize = esp_input_realize;
}

static const TypeInfo esp_input_info = {
    .name = TYPE_ESP_INPUT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESPInputState),
    .instance_init = esp_input_init,
    .class_init = esp_input_class_init,
};

static void esp_input_register_types(void)
{
    type_register_static(&esp_input_info);
}

type_init(esp_input_register_types)
