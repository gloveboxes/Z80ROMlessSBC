#include <stdio.h>
#include <stdlib.h>
#include "GFX.H"

#define TIMER_PORT 0x17

static char command[96];

void gfx_write(const char *text)
{
    while (*text)
        bdos(6, (unsigned char)*text++);
}

static void endcmd(void)
{
    gfx_write(command);
    gfx_write("\033\\");
}

unsigned long gfx_millis(void)
{
    unsigned long value;

    outp(TIMER_PORT, 0x60);
    value = (unsigned long)inp(TIMER_PORT);
    value |= (unsigned long)inp(TIMER_PORT) << 8;
    value |= (unsigned long)inp(TIMER_PORT) << 16;
    value |= (unsigned long)inp(TIMER_PORT) << 24;
    return value;
}

int gfx_open(unsigned width, unsigned height)
{
    static char reply[] = "Z80G1\r";
    unsigned matched;
    unsigned long now;
    int key, rejected;

    inp(TIMER_PORT);
    outp(TIMER_PORT, 0);
    outp(TIMER_PORT, 0x40);
    if (gfx_millis() == 0xFFFFFFFFUL)
        return 0;
    sprintf(command, "\033]777;Z80G;1;open;%u;%u", width, height);
    endcmd();
    matched = 0;
    rejected = 0;
    for (;;) {
        now = gfx_millis();
        if (now == 0xFFFFFFFFUL || now >= 3000UL)
            break;
        if (kbhit()) {
            key = getch();
            if (key == 3 || key == 27 || key == 'q' || key == 'Q')
                break;
            if (matched == 4 && key == '0') {
                rejected = 1;
                ++matched;
            } else if (key == reply[matched]) {
                if (++matched == sizeof(reply) - 1) {
                    if (rejected)
                        break;
                    return 1;
                }
            } else {
                matched = key == reply[0] ? 1 : 0;
                rejected = 0;
            }
        }
    }
    gfx_close();
    return 0;
}

void gfx_close(void)
{
    sprintf(command, "\033]777;Z80G;1;close");
    endcmd();
}

void gfx_clear(unsigned ink)
{
    sprintf(command, "\033]777;Z80G;1;clear;%u", ink);
    endcmd();
}

void gfx_rect(unsigned x, unsigned y, unsigned w, unsigned h, unsigned ink)
{
    sprintf(command, "\033]777;Z80G;1;rect;%u;%u;%u;%u;%u", x, y, w, h, ink);
    endcmd();
}

void gfx_line(unsigned x1, unsigned y1, unsigned x2, unsigned y2, unsigned ink)
{
    sprintf(command, "\033]777;Z80G;1;line;%u;%u;%u;%u;%u", x1, y1, x2, y2, ink);
    endcmd();
}

void gfx_sprite(unsigned id, unsigned w, unsigned h, unsigned ink)
{
    sprintf(command, "\033]777;Z80G;1;sprite;%u;%u;%u;%u", id, w, h, ink);
    endcmd();
}

void gfx_move(unsigned id, unsigned x, unsigned y, unsigned visible)
{
    sprintf(command, "\033]777;Z80G;1;move;%u;%u;%u;%u", id, x, y, visible);
    endcmd();
}

void gfx_present(void)
{
    sprintf(command, "\033]777;Z80G;1;present");
    endcmd();
}
