// Gemini also helped understand what should happen with syscall.c and syslib.S

#include "syslib.h"
#include "blockpixel.h"

// dimensions from snake.c
#define WIDTH 39
#define HEIGHT 11

// Used Gemini to help better understand requiremnts, including that of bp_buffer.
struct stopwatch
{
    // boolean -- whether or not this is running or stopped (black vs red)
    int curr_running;
    // boolean -- whether or not this is the current focus stopwatch (black/red vs blue)
    int has_focus;
    // int64 -- how much time has been accumulated since the previously saved time (cached accumulation)
    uint64_t accumulated;
    // int64 -- the start time, which is either the initial or since the last resume
    uint64_t start_time;
    // blockpixel and buffer stuff.
    struct bp bp;
    uint8_t bp_buffer[WIDTH * HEIGHT * 2];
};

// Used Gemini to help understand how the elapsed time calculation would function.
// Elapsed time is the displayed time, but in nanoseconds. If it is running, then we include the current time - start time to the accumulated, else just the accumulated.
static uint64_t get_elapsed(struct stopwatch *sw)
{
    if (sw->curr_running)
    {
        return sw->accumulated + (user_gettime() - sw->start_time);
    }
    return sw->accumulated;
}

// Used Gemini to help create function (to conveniently make pixels represent numbers through loops)
// Creates digits (0-9) that are 6 blockpixels x 6 blockpixels. We use bp_lazy in order to make all of the updates in the buffer pushed to screen in a single bp_flush
static void draw_6x6_digit(struct bp *bp, int x, int y, int digit, int color)
{
    int w = 6;
    int h = 6;
    int mid = h / 2;

    switch (digit)
    {
    case 0:
        for (int i = 0; i < h; i++)
        {
            bp_put(bp, x, y + i, color, BP_LAZY);
            bp_put(bp, x + w - 1, y + i, color, BP_LAZY);
        }
        for (int i = 1; i < w - 1; i++)
        {
            bp_put(bp, x + i, y, color, BP_LAZY);
            bp_put(bp, x + i, y + h - 1, color, BP_LAZY);
        }
        break;
    case 1:
        for (int i = 0; i < h; i++)
            bp_put(bp, x + w - 1, y + i, color, BP_LAZY);
        break;
    case 2:
        for (int i = 0; i < w; i++)
        {
            bp_put(bp, x + i, y, color, BP_LAZY);
            bp_put(bp, x + i, y + mid, color, BP_LAZY);
            bp_put(bp, x + i, y + h - 1, color, BP_LAZY);
        }
        for (int i = 1; i < mid; i++)
            bp_put(bp, x + w - 1, y + i, color, BP_LAZY);
        for (int i = mid + 1; i < h - 1; i++)
            bp_put(bp, x, y + i, color, BP_LAZY);
        break;
    case 3:
        for (int i = 0; i < w; i++)
        {
            bp_put(bp, x + i, y, color, BP_LAZY);
            bp_put(bp, x + i, y + mid, color, BP_LAZY);
            bp_put(bp, x + i, y + h - 1, color, BP_LAZY);
        }
        for (int i = 0; i < h; i++)
            bp_put(bp, x + w - 1, y + i, color, BP_LAZY);
        break;
    case 4:
        for (int i = 0; i < mid; i++)
            bp_put(bp, x, y + i, color, BP_LAZY);
        for (int i = 0; i < h; i++)
            bp_put(bp, x + w - 1, y + i, color, BP_LAZY);
        for (int i = 1; i < w - 1; i++)
            bp_put(bp, x + i, y + mid, color, BP_LAZY);
        break;
    case 5:
        for (int i = 0; i < w; i++)
        {
            bp_put(bp, x + i, y, color, BP_LAZY);
            bp_put(bp, x + i, y + mid, color, BP_LAZY);
            bp_put(bp, x + i, y + h - 1, color, BP_LAZY);
        }
        for (int i = 1; i < mid; i++)
            bp_put(bp, x, y + i, color, BP_LAZY);
        for (int i = mid + 1; i < h - 1; i++)
            bp_put(bp, x + w - 1, y + i, color, BP_LAZY);
        break;
    case 6:
        for (int i = 0; i < h; i++)
            bp_put(bp, x, y + i, color, BP_LAZY);
        for (int i = 1; i < w; i++)
        {
            bp_put(bp, x + i, y, color, BP_LAZY);
            bp_put(bp, x + i, y + mid, color, BP_LAZY);
            bp_put(bp, x + i, y + h - 1, color, BP_LAZY);
        }
        for (int i = mid + 1; i < h - 1; i++)
            bp_put(bp, x + w - 1, y + i, color, BP_LAZY);
        break;
    case 7:
        for (int i = 0; i < w; i++)
            bp_put(bp, x + i, y, color, BP_LAZY);
        for (int i = 1; i < h; i++)
            bp_put(bp, x + w - 1, y + i, color, BP_LAZY);
        break;
    case 8:
        for (int i = 0; i < h; i++)
        {
            bp_put(bp, x, y + i, color, BP_LAZY);
            bp_put(bp, x + w - 1, y + i, color, BP_LAZY);
        }
        for (int i = 1; i < w - 1; i++)
        {
            bp_put(bp, x + i, y, color, BP_LAZY);
            bp_put(bp, x + i, y + mid, color, BP_LAZY);
            bp_put(bp, x + i, y + h - 1, color, BP_LAZY);
        }
        break;
    case 9:
        for (int i = 0; i < h; i++)
            bp_put(bp, x + w - 1, y + i, color, BP_LAZY);
        for (int i = 0; i < w - 1; i++)
        {
            bp_put(bp, x + i, y, color, BP_LAZY);
            bp_put(bp, x + i, y + mid, color, BP_LAZY);
        }
        for (int i = 1; i < mid; i++)
            bp_put(bp, x, y + i, color, BP_LAZY);
        break;
    }
}

// Used Gemini to help create function (to conveniently deal wiht some of hte background, drawing, and flushing logic.)
//  Creates the actual stopwatch GUI that is rendered. Including numbers and semicolon.
void draw_stopwatch_gui(struct stopwatch *sw, int mins, int secs)
{
    // Borrowed from snake.c, to create the yellow background.
    for (int r = 0; r < HEIGHT; r++)
        for (int c = 0; c < WIDTH; c++)
            user_put(c, r, CELL(' ', ANSI_BLACK, ANSI_YELLOW));

    // to clear out buffered pixels and make them yellow
    for (int i = 0; i < WIDTH * HEIGHT * 2; i++)
        sw->bp_buffer[i] = ANSI_YELLOW;

    // color of the numbers. black if running and in focus. red if not running and in focus. blue if running/not running and out of focus.
    int color;
    if (!sw->has_focus)
    {
        color = ANSI_BLUE;
    }
    else
    {
        color = sw->curr_running ? ANSI_BLACK : ANSI_RED;
    }

    // these are the starting coords, everything grows from this down/to the right.
    int sx = 3, sy = 3;

    int tens_digit_mins = (mins / 10) % 10;
    int ones_digit_mins = mins % 10;
    int tens_digit_secs = (secs / 10) % 10;
    int ones_digit_secs = secs % 10;

    // tens and ones digit for minutes
    draw_6x6_digit(&sw->bp, sx, sy, tens_digit_mins, color);
    draw_6x6_digit(&sw->bp, sx + 7, sy, ones_digit_mins, color);

    // Colon (:)
    bp_put(&sw->bp, sx + 14, sy + 1, color, BP_LAZY);
    bp_put(&sw->bp, sx + 15, sy + 1, color, BP_LAZY);
    bp_put(&sw->bp, sx + 14, sy + 4, color, BP_LAZY);
    bp_put(&sw->bp, sx + 15, sy + 4, color, BP_LAZY);

    // tens and ones digit for seconds
    draw_6x6_digit(&sw->bp, sx + 18, sy, tens_digit_secs, color);
    draw_6x6_digit(&sw->bp, sx + 25, sy, ones_digit_secs, color);

    // actually flushes out it all so the numbers render all at once every call to this.
    bp_flush(&sw->bp);
}

// just calculates the time, really only for separation of logic. Used Gemini for pointer logic.
static void get_time_components(uint64_t elapsed, int *mins, int *secs)
{
    // get seconds from nanoseconds
    uint64_t total_sec = elapsed / 1000000000;
    // note: pointer to mins/secs, so that int values are modified directly here.
    *mins = (int)(total_sec / 60);
    *secs = (int)(total_sec % 60);
}

// Gemini used to help with running loop, and deal with what should happen with teh various user inputs.
void main(void)
{
    // Builds default stopwatch (running and in focus as default
    struct stopwatch sw;
    sw.curr_running = 1;
    sw.has_focus = 1;
    sw.accumulated = 0;
    sw.start_time = user_gettime();

    // using a manual render delay, functionality from pong.c
    int render_delay = 50;
    int t = 0;

    // this is just to initialize the blockpixel struct.
    bp_init(&sw.bp, 0, 1, WIDTH, HEIGHT, sw.bp_buffer);

    for (;;)
    {
        int c;
        int mins, secs;

        // get time without drawing anything yet
        get_time_components(get_elapsed(&sw), &mins, &secs);

        // shuts down cpu (blocks rendering) if not running, else cpu keeps rerunning and redrawing.
        if (sw.curr_running)
        {
            c = user_get(0);
            // waits 50 times to run through this before re-rendering for manaul delay (from pong.c)
            if (++t >= render_delay)
            {
                draw_stopwatch_gui(&sw, mins, secs);
                t = 0;
            }
        }
        else
        {
            c = user_get(1);
        }
        // very similar to snake.c and pong.c

        // if lose focus, draw immediately in blue
        if (c == USER_GET_LOST_FOCUS)
        {
            sw.has_focus = 0;
            draw_stopwatch_gui(&sw, mins, secs);
            // if regain focus, draw immediately in black or red
        }
        else if (c == USER_GET_GOT_FOCUS)
        {
            sw.has_focus = 1;
            draw_stopwatch_gui(&sw, mins, secs);
            // if quit, exit process
        }
        else if (c == 'q')
        {
            user_exit();
            // pause running. Used Gemini to understand what should happen in case that 's' is pressed.
            // Currently, if running program has 's' pressed, then accumulated is updated (+= the difference with the old start time) but running terminated
        }
        else if (c == 's')
        {
            if (sw.curr_running)
            {
                sw.accumulated = get_elapsed(&sw);
                sw.curr_running = 0;
            }
            else
            {
                // if stopped program has 's' pressed then we shift up the whole start time so we get a smaller difference wiht gettime()
                sw.start_time = user_gettime();
                sw.curr_running = 1;
            }
            // Update immediately after state change
            get_time_components(get_elapsed(&sw), &mins, &secs);
            draw_stopwatch_gui(&sw, mins, secs);
        }
        else if (c == 'r')
        {
            // basically restart everything except background rendering.
            sw.accumulated = 0;
            sw.start_time = user_gettime();
            get_time_components(get_elapsed(&sw), &mins, &secs);
            draw_stopwatch_gui(&sw, mins, secs);
        }
    }
}