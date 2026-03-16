#include "syslib.h"
#include "blockpixel.h"
#include "thread.h"

#define WIDTH 39
#define HEIGHT 20

#define BASKET_POS (HEIGHT - 4)
#define BASKET_SIZE 4

struct game
{
    struct sema *mutex;
    int basket;          // horizontal basket position
    int x, y;            // ball position
    int dx;              // direction
    int focus;           // has focus
    int computer, human; // scores
    struct bp bp;
    uint8_t bp_buffer[WIDTH * HEIGHT];
};
// Outputs score character-by-character
void print_score(int x, int score)
{
    if (score == 0)
    {
        user_put(x, 0, CELL('0', ANSI_WHITE, ANSI_BLACK));
        return;
    }
    while (score != 0)
    {
        user_put(x, 0, CELL('0' + score % 10, ANSI_WHITE, ANSI_BLACK));
        score /= 10;
        x--;
    }
}
// Uses block pixel to re-render, and call print_score logic.
void game_redraw(struct game *game)
{
    for (int x = 0; x < WIDTH; x++)
        for (int y = 0; y < HEIGHT; y++)
            bp_put(&game->bp, x, y, ANSI_GREEN, BP_LAZY);
    for (int x = game->basket - BASKET_SIZE / 2; x < game->basket + BASKET_SIZE / 2; x++)
        bp_put(&game->bp, x, BASKET_POS, game->focus ? ANSI_YELLOW : ANSI_BLUE, BP_LAZY);
    bp_put(&game->bp, game->x, game->y, game->focus ? ANSI_RED : ANSI_BLUE, BP_LAZY);
    bp_flush(&game->bp);
    for (int x = 0; x < WIDTH; x++)
        user_put(x, 0, CELL(' ', ANSI_BLACK, ANSI_BLACK));
    print_score(WIDTH / 3, game->computer);
    print_score(WIDTH * 2 / 3, game->human);
}
// Event-driven threading logic. Sleep the thread (block) until you get a key to move basket
void basket_thread(void *arg)
{
    struct game *game = arg;

    for (;;)
    {
        int delta = 0, new_focus = -1;
        int c = thread_get(); // Blocks until a key is pressed
        switch (c)
        {
        case 'q':
            user_exit();
            break;
        case 'a':
            delta = -1;
            break;
        case 'd':
            delta = 1;
            break;
        case 27: // ESC
            c = thread_get();
            if (c != '[' && c != 'O')
                continue;
            c = thread_get();
            switch (c)
            {
            case 'A':
                delta = -1;
                break;
            case 'B':
                delta = 1;
                break;
            }
        // game regains focus. breaks here only exit switch statement.
        // Falls to line 95. if new_focus = 1, accordingly acquires mutex or not, so it has single control and updates the focus of this app and redraws
        case USER_GET_GOT_FOCUS:
            new_focus = 1;
            break;
        case USER_GET_LOST_FOCUS:
            new_focus = 0;
            break;
        }
        // new_focus is default of -1. if got or lost focus still need to redraw either way, just not if focus level is retained.
        if (delta != 0 || new_focus != -1)
        {
            sema_dec(game->mutex);
            game->basket += delta;
            if (new_focus != -1)
                game->focus = new_focus;
            game_redraw(game);
            sema_inc(game->mutex);
        }
    }
}
// ball and game threading logic each share same mutex, so won't render both at same time (race condition).
void ball_thread(void *arg)
{
    struct game *game = arg;

    int delay = 50; // milliseconds
    for (;;)
    {
        thread_sleep(user_gettime() + delay * 1000000ULL);
        // this ball thread acquires the lock.
        sema_dec(game->mutex);
        if (game->y + 1 == BASKET_POS &&
            game->basket - BASKET_SIZE / 2 <= game->x &&
            game->x < game->basket + BASKET_SIZE / 2)
        {
            if (game->x + game->dx >= WIDTH)
            {
                game->x = game->x - game->dx;
            }
            else
            {
                game->x = game->x + game->dx;
            }
            game->y = 0;
            game->human++;
            game->dx++;
        }
        if (game->y == HEIGHT - 1)
        {
            game->y = 0;
            game->computer++;
            delay++;
            if (game->x + (game->dx / 2) >= WIDTH)
            {
                game->x = game->x - (game->dx / 2);
            }
            else
            {
                game->x = game->x + (game->dx / 2);
            }
        }
        game->y++;

        game_redraw(game);
        sema_inc(game->mutex);
    }
}

void main()
{
    // the original thread.
    thread_init();
    struct game game;

    game.mutex = sema_create(1);
    game.basket = HEIGHT / 2;
    game.x = WIDTH / 2;
    game.y = HEIGHT / 2;
    game.focus = 1;
    game.computer = 0;
    game.human = 0;
    bp_init(&game.bp, 0, 1, WIDTH, HEIGHT, game.bp_buffer);
    game_redraw(&game);

    // the original thread init creates ball nad basket threads for runnable, then exits to kill itself.
    thread_create(ball_thread, &game, 16 * 1024);
    thread_create(basket_thread, &game, 16 * 1024);
    thread_exit();
}
// Note expected behavior when tab hit (lost focus): count/score keeps changing, ball keeps falling, but user cannot control any movement of basket and is blue.