#include <stdio.h>
#include "GFX.H"

#define WIDTH 320
#define HEIGHT 240
#define PADDLE_W 48
#define PADDLE_Y 220
#define STAR_W 8

static unsigned random_state = 12345;
static unsigned score, missed;
static int paddle = 136;
static unsigned star_x, star_y;
static char status_line[64];

static void newstar(void)
{
    random_state = random_state * 25173U + 13849U;
    star_x = 8 + random_state % 296U;
    star_y = 32;
}

static void scene(void)
{
    unsigned i;

    gfx_clear(GFX_BLACK);
    gfx_line(0, 24, 319, 24, GFX_BLUE);
    gfx_line(0, 232, 319, 232, GFX_BLUE);
    for (i = 0; i < score; ++i)
        gfx_rect(8 + i * 14, 8, 10, 8, GFX_LIGHT_GREEN);
    for (i = 0; i < 3 - missed; ++i)
        gfx_rect(276 + i * 14, 8, 10, 8, GFX_LIGHT_RED);
}

int main(void)
{
    unsigned long now, next_paddle, next_star;
    int key, quit, timer_error, count, redraw;

    puts("STARFALL - catch 10 stars before missing 3.");
    puts("A/D or Left/Right moves; Q, Esc or Ctrl+C quits.");
    puts("Use the browser graphics terminal; opening canvas...");
    if (!gfx_open(WIDTH, HEIGHT)) {
        puts("Graphics terminal unavailable, cancelled, or timer failed.");
        return 1;
    }
    gfx_sprite(0, PADDLE_W, 8, GFX_LIGHT_CYAN);
    gfx_sprite(1, STAR_W, STAR_W, GFX_YELLOW);
    newstar();
    scene();
    next_paddle = 0;
    next_star = 0;
    quit = 0;
    timer_error = 0;
    while (!quit && score < 10 && missed < 3) {
        for (count = 0; count < 8 && kbhit(); ++count) {
            key = getch();
            if (key == 'a' || key == 'A' || key == 19)
                paddle -= 12;
            if (key == 'd' || key == 'D' || key == 4)
                paddle += 12;
            if (key == 'q' || key == 'Q' || key == 27 || key == 3)
                quit = 1;
        }
        if (paddle < 0)
            paddle = 0;
        if (paddle > WIDTH - PADDLE_W)
            paddle = WIDTH - PADDLE_W;
        now = gfx_millis();
        if (now == 0xFFFFFFFFUL) {
            timer_error = 1;
            break;
        }
        if (quit)
            continue;
        redraw = 0;
        if (now >= next_paddle) {
            next_paddle = now + 50UL;
            gfx_move(0, paddle, PADDLE_Y, 1);
            redraw = 1;
        }
        if (now >= next_star) {
            next_star = now + 50UL;
            star_y += 5;
            if (star_y + STAR_W >= PADDLE_Y) {
                if (star_x + STAR_W > paddle && star_x < paddle + PADDLE_W)
                    ++score;
                else
                    ++missed;
                sprintf(status_line, "Caught %u/10; missed %u/3.\r\n", score, missed);
                gfx_write(status_line);
                if (score == 10 || missed == 3)
                    break;
                newstar();
                scene();
            }
            gfx_move(1, star_x, star_y, 1);
            redraw = 1;
        }
        if (redraw)
            gfx_present();
    }
    gfx_close();
    if (timer_error) {
        puts("Timer read failed; game stopped.");
        return 1;
    }
    if (score == 10)
        puts("You win!");
    else if (missed == 3)
        puts("Game over.");
    else
        puts("Stopped.");
    return 0;
}
