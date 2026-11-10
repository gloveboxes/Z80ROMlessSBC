#ifndef GFX_H
#define GFX_H

#define GFX_BLACK 0
#define GFX_BLUE 1
#define GFX_GREEN 2
#define GFX_CYAN 3
#define GFX_RED 4
#define GFX_MAGENTA 5
#define GFX_BROWN 6
#define GFX_GREY 7
#define GFX_DARK_GREY 8
#define GFX_LIGHT_BLUE 9
#define GFX_LIGHT_GREEN 10
#define GFX_LIGHT_CYAN 11
#define GFX_LIGHT_RED 12
#define GFX_LIGHT_MAGENTA 13
#define GFX_YELLOW 14
#define GFX_WHITE 15

#define gfx_open gxopen
#define gfx_close gxclos
#define gfx_clear gxcler
#define gfx_rect gxrect
#define gfx_line gxline
#define gfx_sprite gxsprt
#define gfx_move gxmove
#define gfx_present gxpres
#define gfx_millis gxms
#define gfx_write gxwr

/* Open waits up to 3 seconds for the browser; it owns stopwatch 0, port 17h.
 * The application must have exclusive console input while graphics is open. */
int gfx_open(unsigned width, unsigned height);
void gfx_close(void);
void gfx_clear(unsigned ink);
void gfx_rect(unsigned x, unsigned y, unsigned w, unsigned h, unsigned ink);
void gfx_line(unsigned x1, unsigned y1, unsigned x2, unsigned y2, unsigned ink);
/* Sprites are retained solid rectangles, IDs 0..31, hidden on definition. */
void gfx_sprite(unsigned id, unsigned w, unsigned h, unsigned ink);
void gfx_move(unsigned id, unsigned x, unsigned y, unsigned visible);
void gfx_present(void);
/* Elapsed milliseconds since open; FFFFFFFFh indicates a timer error. */
unsigned long gfx_millis(void);
/* Direct console output avoids BDOS cooked-output XOFF/Ctrl+C handling. */
void gfx_write(const char *text);

#endif
