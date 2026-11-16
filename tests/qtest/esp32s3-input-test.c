/*
 * QTest for the ESP32-S3 polled keyboard and pointer input device
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define INPUT_BASE      0x60070000
#define INPUT_ID        (INPUT_BASE + 0x00)
#define INPUT_VERSION   (INPUT_BASE + 0x04)
#define INPUT_STATUS    (INPUT_BASE + 0x08)
#define INPUT_POP       (INPUT_BASE + 0x0c)
#define INPUT_KBD0      (INPUT_BASE + 0x10)
#define INPUT_KBD1      (INPUT_BASE + 0x14)
#define INPUT_PTR0      (INPUT_BASE + 0x18)
#define INPUT_PTR1      (INPUT_BASE + 0x1c)

#define STATUS_KBD      0x1
#define STATUS_PTR      0x2

#define USAGE_A         0x04
#define MOD_LEFT_SHIFT  0x02

static void send_key(QTestState *qts, const char *qcode, bool down)
{
    qtest_qmp_assert_success(qts,
        "{'execute': 'input-send-event', 'arguments': {'events': ["
        "{'type': 'key', 'data': {'down': %i,"
        " 'key': {'type': 'qcode', 'data': %s}}}]}}", down, qcode);
}

static void send_button(QTestState *qts, const char *button, bool down)
{
    qtest_qmp_assert_success(qts,
        "{'execute': 'input-send-event', 'arguments': {'events': ["
        "{'type': 'btn', 'data': {'down': %i, 'button': %s}}]}}",
        down, button);
}

/* Both axes and the button state arrive in one event, so one report */
static void send_pointer(QTestState *qts, int x, int y, bool left_down)
{
    qtest_qmp_assert_success(qts,
        "{'execute': 'input-send-event', 'arguments': {'events': ["
        "{'type': 'abs', 'data': {'axis': 'x', 'value': %d}},"
        "{'type': 'abs', 'data': {'axis': 'y', 'value': %d}},"
        "{'type': 'btn', 'data': {'down': %i, 'button': 'left'}}]}}",
        x, y, left_down);
}

static void pop(QTestState *qts, uint32_t what)
{
    qtest_writel(qts, INPUT_POP, what);
}

/* Pop reports until the queue is empty, the last report stays latched */
static void drain(QTestState *qts, uint32_t what)
{
    int guard = 0;

    while (qtest_readl(qts, INPUT_STATUS) & what) {
        pop(qts, what);
        g_assert_cmpint(++guard, <, 64);
    }
}

static void test_id(void)
{
    QTestState *qts = qtest_init("-machine esp32s3");

    g_assert_cmphex(qtest_readl(qts, INPUT_ID), ==, 0x51494E50);
    g_assert_cmphex(qtest_readl(qts, INPUT_VERSION), ==, 1);
    g_assert_cmphex(qtest_readl(qts, INPUT_STATUS), ==, 0);
    g_assert_cmphex(qtest_readl(qts, INPUT_KBD0), ==, 0);
    g_assert_cmphex(qtest_readl(qts, INPUT_PTR0), ==, 0);

    /* Only 32-bit accesses are accepted */
    g_assert_cmphex(qtest_readb(qts, INPUT_ID), ==, 0);
    g_assert_cmphex(qtest_readw(qts, INPUT_ID), ==, 0);
    g_assert_cmphex(qtest_readb(qts, INPUT_VERSION), ==, 0);

    qtest_quit(qts);
}

static void test_key(void)
{
    QTestState *qts = qtest_init("-machine esp32s3");

    send_key(qts, "a", true);
    g_assert_cmphex(qtest_readl(qts, INPUT_STATUS), ==, STATUS_KBD);

    /* A read of POP does not consume the event */
    g_assert_cmphex(qtest_readl(qts, INPUT_POP), ==, 0);
    g_assert_cmphex(qtest_readl(qts, INPUT_STATUS), ==, STATUS_KBD);

    /* Modifiers 0, reserved byte 0, usage of "a" in byte 2 */
    pop(qts, STATUS_KBD);
    g_assert_cmphex(qtest_readl(qts, INPUT_KBD0), ==, USAGE_A << 16);
    g_assert_cmphex(qtest_readl(qts, INPUT_KBD1), ==, 0);
    g_assert_cmphex(qtest_readl(qts, INPUT_STATUS), ==, 0);

    send_key(qts, "a", false);
    g_assert_cmphex(qtest_readl(qts, INPUT_STATUS), ==, STATUS_KBD);
    drain(qts, STATUS_KBD);
    g_assert_cmphex(qtest_readl(qts, INPUT_KBD0), ==, 0);
    g_assert_cmphex(qtest_readl(qts, INPUT_KBD1), ==, 0);

    qtest_quit(qts);
}

static void test_key_shift(void)
{
    QTestState *qts = qtest_init("-machine esp32s3");

    send_key(qts, "shift", true);
    send_key(qts, "a", true);
    drain(qts, STATUS_KBD);
    g_assert_cmphex(qtest_readl(qts, INPUT_KBD0), ==,
                    (USAGE_A << 16) | MOD_LEFT_SHIFT);

    send_key(qts, "a", false);
    send_key(qts, "shift", false);
    drain(qts, STATUS_KBD);
    g_assert_cmphex(qtest_readl(qts, INPUT_KBD0), ==, 0);

    qtest_quit(qts);
}

static void test_pointer(void)
{
    QTestState *qts = qtest_init("-machine esp32s3");

    g_assert_cmphex(qtest_readl(qts, INPUT_STATUS), ==, 0);
    send_pointer(qts, 0x1234, 0x2345, true);
    g_assert_cmphex(qtest_readl(qts, INPUT_STATUS), ==, STATUS_PTR);

    /* buttons, x low, x high, y low | y high, wheel */
    pop(qts, STATUS_PTR);
    g_assert_cmphex(qtest_readl(qts, INPUT_PTR0), ==, 0x45123401);
    g_assert_cmphex(qtest_readl(qts, INPUT_PTR1), ==, 0x0023);
    g_assert_cmphex(qtest_readl(qts, INPUT_STATUS), ==, 0);

    /* The release keeps the position */
    send_button(qts, "left", false);
    g_assert_cmphex(qtest_readl(qts, INPUT_STATUS), ==, STATUS_PTR);
    pop(qts, STATUS_PTR);
    g_assert_cmphex(qtest_readl(qts, INPUT_PTR0), ==, 0x45123400);
    g_assert_cmphex(qtest_readl(qts, INPUT_PTR1), ==, 0x0023);
    g_assert_cmphex(qtest_readl(qts, INPUT_STATUS), ==, 0);

    /* The keyboard is not affected by the pointer */
    g_assert_cmphex(qtest_readl(qts, INPUT_KBD0), ==, 0);

    qtest_quit(qts);
}

static void test_wheel(void)
{
    QTestState *qts = qtest_init("-machine esp32s3");

    send_button(qts, "wheel-up", true);
    g_assert_cmphex(qtest_readl(qts, INPUT_STATUS), ==, STATUS_PTR);
    pop(qts, STATUS_PTR);
    g_assert_cmphex(qtest_readl(qts, INPUT_PTR1) >> 8, ==, 0x01);
    send_button(qts, "wheel-up", false);
    drain(qts, STATUS_PTR);
    g_assert_cmphex(qtest_readl(qts, INPUT_PTR1) >> 8, ==, 0x00);

    send_button(qts, "wheel-down", true);
    g_assert_cmphex(qtest_readl(qts, INPUT_STATUS), ==, STATUS_PTR);
    pop(qts, STATUS_PTR);
    g_assert_cmphex(qtest_readl(qts, INPUT_PTR1) >> 8, ==, 0xff);
    send_button(qts, "wheel-down", false);
    drain(qts, STATUS_PTR);
    g_assert_cmphex(qtest_readl(qts, INPUT_PTR1) >> 8, ==, 0x00);

    qtest_quit(qts);
}

static void test_reset(void)
{
    QTestState *qts = qtest_init("-machine esp32s3");

    send_key(qts, "a", true);
    pop(qts, STATUS_KBD);
    send_pointer(qts, 0x100, 0x200, true);
    g_assert_cmphex(qtest_readl(qts, INPUT_STATUS), ==, STATUS_PTR);

    /* A system reset empties the queues and the latches */
    qtest_qmp_assert_success(qts, "{'execute': 'system_reset'}");
    qtest_qmp_eventwait(qts, "RESET");
    g_assert_cmphex(qtest_readl(qts, INPUT_STATUS), ==, 0);
    g_assert_cmphex(qtest_readl(qts, INPUT_KBD0), ==, 0);
    g_assert_cmphex(qtest_readl(qts, INPUT_PTR0), ==, 0);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/esp32s3/input/id", test_id);
    qtest_add_func("/esp32s3/input/key", test_key);
    qtest_add_func("/esp32s3/input/key_shift", test_key_shift);
    qtest_add_func("/esp32s3/input/pointer", test_pointer);
    qtest_add_func("/esp32s3/input/wheel", test_wheel);
    qtest_add_func("/esp32s3/input/reset", test_reset);
    return g_test_run();
}
