/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * Pong Wars for Atari Lynx
 *
 * - The Lynx's 160x102 screen, double buffered through cc65's TGI driver
 * - 100x100 game area, centred: a 10x10 grid of 10-pixel squares
 * - Left half = "day", right half = "night"
 * - Day & night counters in white text to left/right
 *
 * A port of Pong Wars for Atari ST, which plays on a 200x200 field at
 * 50 Hz. Here the game runs on a 240x240 field at the Lynx's 60 Hz,
 * which keeps the ST's ball speeds and bounces exactly, and every 24
 * units become 10 pixels on screen.
 *
 * Everything on screen is a sprite in one chain -- the squares, the
 * balls and the counters -- which Suzy draws in a single pass each
 * frame. The game only changes the sprites that moved or changed colour.
 *
 * @author  Neil Rackett <https://github.com/neilrackett>
 */

#include <6502.h>   /* CLI */
#include <lynx.h>   /* SCBs, tgi_sprite, tgi_busy, tgi_updatedisplay */
#include <stdlib.h> /* rand */
#include <tgi.h>
#include <time.h>   /* clock */

/* --- Screen/geometry constants ------------------------------------- */

#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 102

#define SPLASH_SECONDS 3

#define GRID_SIZE 10
#define SQUARE_SIZE 10                        /* pixels */
#define GAME_PIXELS (GRID_SIZE * SQUARE_SIZE) /* result should be 100 */

#define GAME_LEFT ((SCREEN_WIDTH - GAME_PIXELS) / 2)
#define GAME_TOP ((SCREEN_HEIGHT - GAME_PIXELS) / 2)

/* The field the game is played on, in units of its own: 24 to a square,
 * where a square is 10 pixels. */
#define SQUARE_UNITS 24
#define GAME_UNITS (GRID_SIZE * SQUARE_UNITS) /* 240 */
#define UNITS_TO_PIXELS(u) ((u) * SQUARE_SIZE / SQUARE_UNITS)

#define BALL_SIZE 12     /* units: 5 pixels */
#define BALL_VELOCITY 10 /* units per frame */

#define DIGIT_WIDTH 5
#define DIGIT_HEIGHT 7
#define MAX_SCORE_DIGITS 3

#define SCORE_AREA_WIDTH (GAME_LEFT)
#define MAX_SCORE_WIDTH (MAX_SCORE_DIGITS * (DIGIT_WIDTH + 1) - 1)

#define LEFT_SCORE_X ((SCORE_AREA_WIDTH - MAX_SCORE_WIDTH) / 2)
#define RIGHT_SCORE_X (GAME_LEFT + GAME_PIXELS + (SCORE_AREA_WIDTH - MAX_SCORE_WIDTH) / 2 + DIGIT_WIDTH)
#define SCORE_CENTER_Y (SCREEN_HEIGHT / 2)
#define SCORE_TOP (SCORE_CENTER_Y - DIGIT_HEIGHT / 2)

/* --- Colour indices and palette ------------------------------------ */

#define COLOR_BG 0 /* background / borders */

/* The original's scheme: DAY_COLOR / NIGHT_COLOR and ball colours */
#define DAY_COLOR 1        /* #D9E8E3 */
#define NIGHT_COLOR 2      /* #172B36 */
#define DAY_BALL_COLOR 3   /* dark ball on day side  */
#define NIGHT_BALL_COLOR 4 /* light ball on night side */

/* The background again, for the counters' backgrounds: a sprite cannot
 * draw colour 0, which Suzy treats as transparent. */
#define COLOR_BG_SPRITE 5

#define COLOR_TEXT 15 /* white text */

/* tgi_setpalette() takes the Lynx's own colour registers: 16 GREEN, then
 * 16 BLUERED, 4 bits per channel. (The second level of macro lets an
 * RGB_ name expand into its three channels first.) */
#define GREEN_(r, g, b) (g)
#define BLUERED_(r, g, b) (((b) << 4) | (r))
#define GREEN(rgb) GREEN_(rgb)
#define BLUERED(rgb) BLUERED_(rgb)

#define RGB_BG 0, 2, 2
#define RGB_DAY 13, 14, 13
#define RGB_NIGHT 1, 3, 3
#define RGB_TEXT 15, 15, 15

static const unsigned char game_palette[32] = {
    GREEN(RGB_BG), GREEN(RGB_DAY), GREEN(RGB_NIGHT), GREEN(RGB_NIGHT),
    GREEN(RGB_DAY), GREEN(RGB_BG), 0, 0, 0, 0, 0, 0, 0, 0, 0, GREEN(RGB_TEXT),
    BLUERED(RGB_BG), BLUERED(RGB_DAY), BLUERED(RGB_NIGHT), BLUERED(RGB_NIGHT),
    BLUERED(RGB_DAY), BLUERED(RGB_BG), 0, 0, 0, 0, 0, 0, 0, 0, 0, BLUERED(RGB_TEXT)};

/* --- Ownership enum for board cells -------------------------------- */

enum
{
  OWNER_DAY = 1,
  OWNER_NIGHT = 2
};

/* --- Globals -------------------------------------------------------- */

/* Territory grid ("squares") */
static unsigned char squares[GRID_SIZE][GRID_SIZE];
static int dayScore = 0;
static int nightScore = 0;

/* --- Ball structure ("balls" array like original) ------------------ */
/* x,y are coordinates on the game field (0..GAME_UNITS-1), vx,vy are
   velocities in units per frame. */

typedef struct
{
  int x, y;   /* top-left in game units */
  int vx, vy; /* velocity in units per frame */
  int owner;  /* OWNER_DAY / OWNER_NIGHT */
  int color;  /* DAY_BALL_COLOR / NIGHT_BALL_COLOR */
} Ball;

static Ball balls[2];

/* --- Splash screen (build/splash.c, made from splash.png) ---------- */

extern const unsigned char splash_palette[32];
extern unsigned char splash_sprite[];

static SCB_REHV_PAL splash_scb = {
    BPP_4 | TYPE_BACKNONCOLL, LITERAL | REHV, 0x20, 0, splash_sprite,
    0, 0, 0x0100, 0x0100,
    {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF}};

/* --- Digit font (5x7) ---------------------------------------------- */

static const unsigned char digit_font[10][DIGIT_HEIGHT] = {
    /* 0 */
    {0x1E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x1E},
    /* 1 */
    {0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F},
    /* 2 */
    {0x1E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F},
    /* 3 */
    {0x1E, 0x11, 0x01, 0x0E, 0x01, 0x11, 0x1E},
    /* 4 */
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    /* 5 */
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x1E},
    /* 6 */
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x1E},
    /* 7 */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    /* 8 */
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
    /* 9 */
    {0x1E, 0x11, 0x11, 0x1F, 0x01, 0x02, 0x0C}};

/* Each digit as a 1-bit literal sprite, built from digit_font at start:
 * per row, a length byte (3: itself and two data bytes), the row's five
 * pixels from the left in the top bits, and a byte of padding; then a
 * zero to end it. Drawn white, the clear pixels transparent. */
#define DIGIT_SPRITE_BYTES (DIGIT_HEIGHT * 3 + 1)

static unsigned char digit_sprites[10][DIGIT_SPRITE_BYTES];

static void init_digits(void)
{
  unsigned char d, row;
  for (d = 0; d < 10; ++d)
  {
    unsigned char *p = digit_sprites[d];
    for (row = 0; row < DIGIT_HEIGHT; ++row)
    {
      *p++ = 3;
      *p++ = (unsigned char)(digit_font[d][row] << (8 - DIGIT_WIDTH));
      *p++ = 0;
    }
    *p = 0;
  }
}

/* --- The sprite chain ----------------------------------------------- */

/* A single 1-bit pixel, which Suzy scales up into any rectangle: the
 * rest of its byte is transparent pen 0, so only the one pixel draws. */
static unsigned char pixel_sprite[] = {2, 0x80, 0};

/* Drawn in this order, each later one over the earlier: the counters'
 * backgrounds and digits, the squares, then the balls. */
#define CHAIN_LENGTH (2 + 2 * MAX_SCORE_DIGITS + GRID_SIZE * GRID_SIZE + 2)

static SCB_REHV_PAL chain[CHAIN_LENGTH];

static SCB_REHV_PAL *const score_bg = &chain[0];                            /* 2 */
static SCB_REHV_PAL *const score_digits = &chain[2];                        /* 2 x 3 */
static SCB_REHV_PAL *const cells = &chain[2 + 2 * MAX_SCORE_DIGITS];        /* 10 x 10 */
static SCB_REHV_PAL *const ball_sprites = &chain[CHAIN_LENGTH - 2];         /* 2 */

/* One sprite of the chain: a 1-bit sprite whose pen 1 is `color` */
static void init_sprite(SCB_REHV_PAL *s, unsigned char *data, int x, int y,
                        unsigned int hsize, unsigned int vsize, unsigned char color)
{
  s->sprctl0 = BPP_1 | TYPE_NONCOLL;
  s->sprctl1 = LITERAL | REHV;
  s->sprcoll = 0x20;
  s->next = (s == &chain[CHAIN_LENGTH - 1]) ? 0 : (char *)(s + 1);
  s->data = data;
  s->hpos = x;
  s->vpos = y;
  s->hsize = hsize;
  s->vsize = vsize;
  s->penpal[0] = color; /* pen 0 stays transparent */
}

/* A solid rectangle: the one pixel, scaled to w x h */
static void init_rect(SCB_REHV_PAL *s, int x, int y, unsigned char w,
                      unsigned char h, unsigned char color)
{
  init_sprite(s, pixel_sprite, x, y, (unsigned int)w << 8, (unsigned int)h << 8, color);
}

static void init_chain(void)
{
  unsigned char i, gx, gy;

  init_rect(&score_bg[0], LEFT_SCORE_X, SCORE_TOP, MAX_SCORE_WIDTH, DIGIT_HEIGHT, COLOR_BG_SPRITE);
  init_rect(&score_bg[1], RIGHT_SCORE_X, SCORE_TOP, MAX_SCORE_WIDTH, DIGIT_HEIGHT, COLOR_BG_SPRITE);

  for (i = 0; i < MAX_SCORE_DIGITS; ++i)
  {
    int offset = i * (DIGIT_WIDTH + 1);
    init_sprite(&score_digits[i], digit_sprites[0], LEFT_SCORE_X + offset, SCORE_TOP,
                0x0100, 0x0100, COLOR_TEXT);
    init_sprite(&score_digits[MAX_SCORE_DIGITS + i], digit_sprites[0],
                RIGHT_SCORE_X + offset, SCORE_TOP, 0x0100, 0x0100, COLOR_TEXT);
  }

  for (gy = 0; gy < GRID_SIZE; ++gy)
  {
    for (gx = 0; gx < GRID_SIZE; ++gx)
    {
      init_rect(&cells[gy * GRID_SIZE + gx], GAME_LEFT + gx * SQUARE_SIZE,
                GAME_TOP + gy * SQUARE_SIZE, SQUARE_SIZE, SQUARE_SIZE, NIGHT_COLOR);
    }
  }

  for (i = 0; i < 2; ++i)
  {
    init_rect(&ball_sprites[i], 0, 0, UNITS_TO_PIXELS(BALL_SIZE),
              UNITS_TO_PIXELS(BALL_SIZE), balls[i].color);
  }
}

/* --- Board / cells ("squares") ------------------------------------- */

static void set_cell(int gx, int gy, int owner)
{
  squares[gy][gx] = (unsigned char)owner;
  cells[gy * GRID_SIZE + gx].penpal[0] = (owner == OWNER_DAY) ? DAY_COLOR : NIGHT_COLOR;
}

static void init_board(void)
{
  unsigned char x, y;
  dayScore = nightScore = 0;

  for (y = 0; y < GRID_SIZE; ++y)
  {
    for (x = 0; x < GRID_SIZE; ++x)
    {
      unsigned char owner = (x < GRID_SIZE / 2) ? OWNER_DAY : OWNER_NIGHT;
      set_cell(x, y, owner);
      if (owner == OWNER_DAY)
      {
        dayScore++;
      }
      else
      {
        nightScore++;
      }
    }
  }
}

/* Paint a cell for given owner and update scores */
static void paint_cell(int gx, int gy, int owner)
{
  if (gx < 0 || gx >= GRID_SIZE || gy < 0 || gy >= GRID_SIZE)
  {
    return;
  }

  if (squares[gy][gx] == owner)
  {
    return;
  }

  if (squares[gy][gx] == OWNER_DAY)
  {
    dayScore--;
  }
  else if (squares[gy][gx] == OWNER_NIGHT)
  {
    nightScore--;
  }

  set_cell(gx, gy, owner);

  if (owner == OWNER_DAY)
  {
    dayScore++;
  }
  else if (owner == OWNER_NIGHT)
  {
    nightScore++;
  }
}

/* --- Balls ---------------------------------------------------------- */

static void init_balls(void)
{
  /* day ball (left) */
  balls[0].x = GAME_UNITS / 4; /* 60 */
  balls[0].y = GAME_UNITS / 2; /* 120 */
  balls[0].vx = BALL_VELOCITY / 2;
  balls[0].vy = BALL_VELOCITY;
  balls[0].owner = OWNER_DAY;
  balls[0].color = DAY_BALL_COLOR;

  /* night ball (right) */
  balls[1].x = (GAME_UNITS * 3) / 4; /* 180 */
  balls[1].y = GAME_UNITS / 2;       /* 120 */
  balls[1].vx = -BALL_VELOCITY;
  balls[1].vy = -BALL_VELOCITY / 2;
  balls[1].owner = OWNER_NIGHT;
  balls[1].color = NIGHT_BALL_COLOR;
}

/**
 * Slightly vary the bounce angle while maintaining overall velocity.
 */
static void perturb_velocity(Ball *ball)
{
  /* 50% chance to vary angle */
  if (rand() & 1)
  {
    int ax = (ball->vx < 0) ? -ball->vx : ball->vx;
    int ay = (ball->vy < 0) ? -ball->vy : ball->vy;

    if (rand() & 1)
    {
      ax++;
      ay--;
    }
    else
    {
      ax--;
      ay++;
    }

    /* Clamp to maintain approximate velocity */
    /* Range [4, 11] keeps speed squared between ~113 and ~137 */
    if (ax < 4)
    {
      ax = 4;
      ay = 11;
    }
    if (ay < 4)
    {
      ay = 4;
      ax = 11;
    }
    if (ax > 11)
    {
      ax = 11;
      ay = 4;
    }
    if (ay > 11)
    {
      ay = 11;
      ax = 4;
    }

    ball->vx = (ball->vx < 0) ? -ax : ax;
    ball->vy = (ball->vy < 0) ? -ay : ay;
  }
}

/**
 * Reflect off outer edges of the game area.
 */
static void checkBoundaryCollision(Ball *ball)
{
  if (ball->x + ball->vx < 0 || ball->x + BALL_SIZE + ball->vx > GAME_UNITS)
  {
    ball->vx = -ball->vx;
    perturb_velocity(ball);
  }
  if (ball->y + ball->vy < 0 || ball->y + BALL_SIZE + ball->vy > GAME_UNITS)
  {
    ball->vy = -ball->vy;
    perturb_velocity(ball);
  }
}

/**
 * Ball moves freely in units, but territory is on a 10x10 grid.
 * When the ball's centre enters an enemy-owned cell, we repaint it and bounce.
 */
static void checkSquareCollision(Ball *ball, int old_gx, int old_gy)
{
  int center_x = ball->x + BALL_SIZE / 2;
  int center_y = ball->y + BALL_SIZE / 2;

  int gx = center_x / SQUARE_UNITS;
  int gy = center_y / SQUARE_UNITS;

  if (gx < 0 || gx >= GRID_SIZE || gy < 0 || gy >= GRID_SIZE)
  {
    return;
  }

  if (squares[gy][gx] != ball->owner)
  {
    /* capture territory */
    paint_cell(gx, gy, ball->owner);

    /* decide bounce axis based on grid movement */
    {
      int dxg = gx - old_gx;
      int dyg = gy - old_gy;

      if (dxg != 0 && dyg == 0)
      {
        ball->vx = -ball->vx; /* horizontal wall */
      }
      else if (dxg == 0 && dyg != 0)
      {
        ball->vy = -ball->vy; /* vertical wall   */
      }
      else
      {
        ball->vx = -ball->vx; /* corner/ambiguous */
        ball->vy = -ball->vy;
      }

      perturb_velocity(ball);
    }
  }
}

/**
 * Ball/ball collision on the game field
 */
static void handle_ball_collision(void)
{
  int dx = balls[0].x - balls[1].x;
  int dy = balls[0].y - balls[1].y;

  if (dx < 0)
    dx = -dx;
  if (dy < 0)
    dy = -dy;

  if (dx < BALL_SIZE && dy < BALL_SIZE)
  {
    /* simple exchange of velocities */
    int tmpvx = balls[0].vx;
    int tmpvy = balls[0].vy;
    balls[0].vx = balls[1].vx;
    balls[0].vy = balls[1].vy;
    balls[1].vx = tmpvx;
    balls[1].vy = tmpvy;
  }
}

/* --- Counters ------------------------------------------------------- */

/* Show `value` left-aligned in a counter's three digit sprites, skipping
 * the ones it doesn't need. */
static void set_number(SCB_REHV_PAL *digits, int value)
{
  unsigned char buf[MAX_SCORE_DIGITS];
  unsigned char len = 0, i;

  do
  {
    buf[len++] = (unsigned char)(value % 10);
    value /= 10;
  } while (value > 0 && len < MAX_SCORE_DIGITS);

  for (i = 0; i < MAX_SCORE_DIGITS; ++i)
  {
    if (i < len)
    {
      digits[i].data = digit_sprites[buf[len - 1 - i]];
      digits[i].sprctl1 = LITERAL | REHV;
    }
    else
    {
      digits[i].sprctl1 = LITERAL | REHV | SKIP;
    }
  }
}

/* --- One frame of the simulation ------------------------------------ */

/**
 * Move the balls on by one frame
 */
static void update(void)
{
  int i;

  /* Move balls and territory collisions */
  for (i = 0; i < 2; ++i)
  {
    int center_x_old = balls[i].x + BALL_SIZE / 2;
    int center_y_old = balls[i].y + BALL_SIZE / 2;
    int old_gx = center_x_old / SQUARE_UNITS;
    int old_gy = center_y_old / SQUARE_UNITS;

    checkBoundaryCollision(&balls[i]);

    balls[i].x += balls[i].vx;
    balls[i].y += balls[i].vy;

    /* clamp for safety */
    if (balls[i].x < 0)
    {
      balls[i].x = 0;
      balls[i].vx = -balls[i].vx;
    }
    if (balls[i].y < 0)
    {
      balls[i].y = 0;
      balls[i].vy = -balls[i].vy;
    }
    if (balls[i].x > GAME_UNITS - BALL_SIZE)
    {
      balls[i].x = GAME_UNITS - BALL_SIZE;
      balls[i].vx = -balls[i].vx;
    }
    if (balls[i].y > GAME_UNITS - BALL_SIZE)
    {
      balls[i].y = GAME_UNITS - BALL_SIZE;
      balls[i].vy = -balls[i].vy;
    }

    checkSquareCollision(&balls[i], old_gx, old_gy);
  }

  /* Ball/ball collision */
  handle_ball_collision();
}

/**
 * Draw the frame into the page that isn't on screen, in one pass of the
 * sprite chain. The margins only need clearing on the first frame of
 * each page, to remove the splash; after that only the counters change.
 */
static void draw(void)
{
  static unsigned char cleared;
  unsigned char i;

  if (cleared < 2)
  {
    tgi_setcolor(COLOR_BG);
    tgi_bar(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
    cleared++;
  }

  for (i = 0; i < 2; ++i)
  {
    ball_sprites[i].hpos = GAME_LEFT + UNITS_TO_PIXELS(balls[i].x);
    ball_sprites[i].vpos = GAME_TOP + UNITS_TO_PIXELS(balls[i].y);
  }
  set_number(&score_digits[0], dayScore);
  set_number(&score_digits[MAX_SCORE_DIGITS], nightScore);

  tgi_sprite(&chain[0]);
}

/* Show the page just drawn at the next vertical blank, and wait for it */
static void present(void)
{
  tgi_updatedisplay();
  while (tgi_busy())
  {
  }
}

/* --- Main ----------------------------------------------------------- */

int main(void)
{
  clock_t start;

  tgi_install(tgi_static_stddrv);
  tgi_init();
  CLI();
  while (tgi_busy())
  {
  }
  tgi_setframerate(60);
  tgi_setcollisiondetection(0);

  /* Splash screen, while the game is set up behind it */
  tgi_setpalette(splash_palette);
  tgi_sprite(&splash_scb);
  present();
  start = clock();

  init_digits();
  init_balls();
  init_chain();
  init_board();

  while (clock() - start < SPLASH_SECONDS * CLOCKS_PER_SEC)
  {
  }

  /* First frame, then the game's colours once it is on screen */
  draw();
  present();
  tgi_setpalette(game_palette);

  for (;;)
  {
    update();
    draw();
    present();
  }

  return 0;
}
