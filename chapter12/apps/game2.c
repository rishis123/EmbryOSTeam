#include "syslib.h"
#include "blockpixel.h"
#include "thread.h"

#define WIDTH 39
#define HEIGHT 20

#define PADDLE_POS (HEIGHT - 4)
#define PADDLE_SIZE 4

struct pong
{
  struct sema *mutex;
  int paddle;          // vertical paddle position
  int x, y;            // ball position
  int dx;              // direction
  int focus;           // has focus
  int computer, human; // scores
  struct bp bp;
  uint8_t bp_buffer[WIDTH * HEIGHT];
};
//Outputs score character-by-character
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
//Uses block pixel to re-render, and call print_score logic.
void pong_redraw(struct pong *pong)
{
  for (int x = 0; x < WIDTH; x++)
    for (int y = 0; y < HEIGHT; y++)
      bp_put(&pong->bp, x, y, ANSI_GREEN, BP_LAZY);
  for (int x = pong->paddle - PADDLE_SIZE / 2; x < pong->paddle + PADDLE_SIZE / 2; x++)
    bp_put(&pong->bp, x, PADDLE_POS, pong->focus ? ANSI_YELLOW : ANSI_BLUE, BP_LAZY);
  bp_put(&pong->bp, pong->x, pong->y, pong->focus ? ANSI_RED : ANSI_BLUE, BP_LAZY);
  bp_flush(&pong->bp);
  for (int x = 0; x < WIDTH; x++)
    user_put(x, 0, CELL(' ', ANSI_BLACK, ANSI_BLACK));
  print_score(WIDTH / 3, pong->computer);
  print_score(WIDTH * 2 / 3, pong->human);
}
// Event-driven threading logic. Sleep the thread (block) until you get a key to move paddle
void paddle_thread(void *arg)
{
  struct pong *pong = arg;

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
    //pong regains focus. breaks here only exit switch statement.
    //Falls to line 95. if new_focus = 1, accordingly acquires mutex or not, so it has single control and updates the focus of this app and redraws
    case USER_GET_GOT_FOCUS:
      new_focus = 1;
      break;
    case USER_GET_LOST_FOCUS:
      new_focus = 0;
      break;
    }
    //new_focus is default of -1. if got or lost focus still need to redraw either way, just not if focus level is retained.
    if (delta != 0 || new_focus != -1)
    {
      sema_dec(pong->mutex);
      pong->paddle += delta;
      if (new_focus != -1)
        pong->focus = new_focus;
      pong_redraw(pong);
      sema_inc(pong->mutex);
    }
  }
}
//ball and pong threading logic each share same mutex, so won't render both at same time (race condition).
void ball_thread(void *arg)
{
  struct pong *pong = arg;

  int delay = 50; // milliseconds
  for (;;)
  {
    thread_sleep(user_gettime() + delay * 1000000ULL);
    //this ball thread acquires the lock.
    sema_dec(pong->mutex);
    if (pong->y + 1 == PADDLE_POS &&
        pong->paddle - PADDLE_SIZE / 2 <= pong->x &&
        pong->x < pong->paddle + PADDLE_SIZE / 2)
    {
      if (pong->x + pong->dx >= WIDTH)
      {
        pong->x = pong->x - pong->dx;
      }
      else
      {
        pong->x = pong->x + pong->dx;
      }
      pong->y = 0;
      pong->human++;
      pong->dx++;
    }
    if (pong->y == HEIGHT - 1)
    {
      pong->y = 0;
      pong->computer++;
      delay++;
      if (pong->x + (pong->dx / 2) >= WIDTH)
      {
        pong->x = pong->x - (pong->dx / 2);
      }
      else
      {
        pong->x = pong->x + (pong->dx / 2);
      }
    }
    pong->y++;

    pong_redraw(pong);
    sema_inc(pong->mutex);
  }
}

void main()
{
  //the original thread.
  thread_init();
  struct pong pong;

  pong.mutex = sema_create(1);
  pong.paddle = HEIGHT / 2;
  pong.x = WIDTH / 2;
  pong.y = HEIGHT / 2;
  pong.focus = 1;
  pong.computer = 0;
  pong.human = 0;
  bp_init(&pong.bp, 0, 1, WIDTH, HEIGHT, pong.bp_buffer);
  pong_redraw(&pong);

  //the original thread init creates ball nad paddle threads for runnable, then exits to kill itself.
  thread_create(ball_thread, &pong, 16 * 1024);
  thread_create(paddle_thread, &pong, 16 * 1024);
  thread_exit();
}
//Note expected behavior when tab hit (lost focus): count/score keeps changing, ball keeps falling, but user cannot control any movement of paddle and is blue.