// Commands for sending messages to an st7920 lcd driver
//
// Copyright (C) 2018  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "basecmd.h" // oid_alloc
#include "board/gpio.h" // gpio_out_write
#include "board/irq.h" // irq_poll
#include "board/misc.h" // timer_from_us
#include "command.h" // DECL_COMMAND
#include "sched.h" // DECL_SHUTDOWN

struct st7920 {
    uint32_t last_cmd_time, cmd_wait_ticks;
    struct gpio_out cs, sclk, sid;
};


/****************************************************************
 * Transmit functions
 ****************************************************************/

#define SYNC_CMD  0xf8
#define SYNC_DATA 0xfa

// Write eight bits to the st7920 via the serial interface
static void
st7920_xmit_byte(struct st7920 *s, uint8_t data)
{
    struct gpio_out sclk = s->sclk, sid = s->sid;
    uint8_t bits = 8;
    while (--bits) {
        if (data & 0x80) {
            data = ~data;
            gpio_out_toggle(sid);
        }
        gpio_out_toggle(sclk);
        data <<= 1;
        gpio_out_toggle(sclk);
    }
}

// Transmit a series of command bytes to the chip
static void
st7920_xmit(struct st7920 *s, uint8_t sync, uint8_t count, uint8_t *cmds)
{
    gpio_out_toggle(s->cs);
    st7920_xmit_byte(s, sync);
    uint32_t last_cmd_time=s->last_cmd_time, cmd_wait_ticks=s->cmd_wait_ticks;
    while (count--) {
        uint8_t cmd = *cmds++;
        st7920_xmit_byte(s, cmd & 0xf0);
        // Can't complete transfer until delay complete
        while (timer_read_time() - last_cmd_time < cmd_wait_ticks)
            irq_poll();
        st7920_xmit_byte(s, cmd << 4);
        last_cmd_time = timer_read_time();
    }
    gpio_out_toggle(s->cs);
    s->last_cmd_time = last_cmd_time;
}


/****************************************************************
 * Interface
 ****************************************************************/

void
command_config_st7920(uint32_t *args)
{
    struct st7920 *s = oid_alloc(args[0], command_config_st7920, sizeof(*s));
    s->cs = gpio_out_setup(args[1], 0);
    s->sclk = gpio_out_setup(args[2], 0);
    s->sid = gpio_out_setup(args[3], 0);

    // Calibrate cmd_wait_ticks
    gpio_out_toggle(s->cs);
    st7920_xmit_byte(s, SYNC_CMD);
    st7920_xmit_byte(s, 0x20);
    irq_disable();
    uint32_t start = timer_read_time();
    st7920_xmit_byte(s, 0x00);
    uint32_t end = timer_read_time();
    irq_enable();
    s->last_cmd_time = end;
    uint32_t diff = end - start, delay_ticks = args[4];
    if (diff < delay_ticks)
        s->cmd_wait_ticks = delay_ticks - diff;
    gpio_out_toggle(s->cs);
}
DECL_COMMAND(command_config_st7920,
             "config_st7920 oid=%c cs_pin=%u sclk_pin=%u sid_pin=%u"
             " delay_ticks=%u");

void
command_st7920_send_cmds(uint32_t *args)
{
    struct st7920 *s = oid_lookup(args[0], command_config_st7920);
    uint8_t len = args[1], *cmds = (void*)(size_t)args[2];
    st7920_xmit(s, SYNC_CMD, len, cmds);
}
DECL_COMMAND(command_st7920_send_cmds, "st7920_send_cmds oid=%c cmds=%*s");

void
command_st7920_send_data(uint32_t *args)
{
    struct st7920 *s = oid_lookup(args[0], command_config_st7920);
    uint8_t len = args[1], *data = (void*)(size_t)args[2];
    st7920_xmit(s, SYNC_DATA, len, data);
}
DECL_COMMAND(command_st7920_send_data, "st7920_send_data oid=%c data=%*s");

void
st7920_shutdown(void)
{
    uint8_t i;
    struct st7920 *s;
    foreach_oid(i, s, command_config_st7920) {
        gpio_out_write(s->cs, 0);
        gpio_out_write(s->sclk, 0);
        gpio_out_write(s->sid, 0);
    }
}
DECL_SHUTDOWN(st7920_shutdown);
