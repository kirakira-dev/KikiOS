/*
 * KikiOS Snake Game (Userspace)
 *
 * Classic snake game for the terminal.
 * Controls: WASD to move, Q to quit
 */

#include "kiki.h"

// Game board dimensions
#define BOARD_WIDTH  40
#define BOARD_HEIGHT 20
#define BOARD_X      10
#define BOARD_Y      2

// Maximum snake length
#define MAX_SNAKE_LEN 256

// Directions
#define DIR_UP    0
#define DIR_DOWN  1
#define DIR_LEFT  2
#define DIR_RIGHT 3

// Point structure
typedef struct {
    int x;
    int y;
} point_t;

// Game state
static kapi_t *api;
static point_t snake[MAX_SNAKE_LEN];
static int snake_len;
static int direction;
static int next_direction;
static point_t food;
static int score;
static int game_over;
static int high_score = 0;

// Random number generator
static uint32_t rand_state = 12345;

static uint32_t rand(void) {
    rand_state = rand_state * 1103515245 + 12345;
    return (rand_state >> 16) & 0x7FFF;
}

// Game speed delay in milliseconds
#define SNAKE_DELAY_MS 100

// Draw the border
static void draw_border(void) {
    api->set_color(COLOR_WHITE, COLOR_BLACK);

    // Top border
    api->set_cursor(BOARD_Y - 1, BOARD_X - 1);
    api->putc('+');
    for (int x = 0; x < BOARD_WIDTH; x++) {
        api->putc('-');
    }
    api->putc('+');

    // Side borders
    for (int y = 0; y < BOARD_HEIGHT; y++) {
        api->set_cursor(BOARD_Y + y, BOARD_X - 1);
        api->putc('|');
        api->set_cursor(BOARD_Y + y, BOARD_X + BOARD_WIDTH);
        api->putc('|');
    }

    // Bottom border
    api->set_cursor(BOARD_Y + BOARD_HEIGHT, BOARD_X - 1);
    api->putc('+');
    for (int x = 0; x < BOARD_WIDTH; x++) {
        api->putc('-');
    }
    api->putc('+');
}

// Draw the score
static void draw_score(void) {
    api->set_cursor(0, 0);
    api->set_color(COLOR_AMBER, COLOR_BLACK);
    api->puts("SNAKE");
    api->set_color(COLOR_WHITE, COLOR_BLACK);
    api->puts("  Score: ");
    api->print_int(score);
    api->puts("  High: ");
    api->print_int(high_score);
    api->puts("  [Q]uit");
    // Clear rest of line
    for (int i = 0; i < 20; i++) api->putc(' ');
}

// Place food at a random location
static void place_food(void) {
    int valid;
    do {
        valid = 1;
        food.x = rand() % BOARD_WIDTH;
        food.y = rand() % BOARD_HEIGHT;

        for (int i = 0; i < snake_len; i++) {
            if (snake[i].x == food.x && snake[i].y == food.y) {
                valid = 0;
                break;
            }
        }
    } while (!valid);
}

// Draw a cell
static void draw_cell(int x, int y, char c, uint32_t color) {
    api->set_cursor(BOARD_Y + y, BOARD_X + x);
    api->set_color(color, COLOR_BLACK);
    api->putc(c);
}

// Clear a cell
static void clear_cell(int x, int y) {
    api->set_cursor(BOARD_Y + y, BOARD_X + x);
    api->set_color(COLOR_BLACK, COLOR_BLACK);
    api->putc(' ');
}

// Draw the snake
static void draw_snake(void) {
    draw_cell(snake[0].x, snake[0].y, '@', COLOR_GREEN);
    for (int i = 1; i < snake_len; i++) {
        draw_cell(snake[i].x, snake[i].y, 'o', COLOR_GREEN);
    }
}

// Draw food
static void draw_food(void) {
    draw_cell(food.x, food.y, '*', COLOR_RED);
}

// Initialize the game
static void init_game(void) {
    api->clear();

    snake_len = 3;
    snake[0].x = BOARD_WIDTH / 2;
    snake[0].y = BOARD_HEIGHT / 2;
    snake[1].x = snake[0].x - 1;
    snake[1].y = snake[0].y;
    snake[2].x = snake[0].x - 2;
    snake[2].y = snake[0].y;

    direction = DIR_RIGHT;
    next_direction = DIR_RIGHT;
    score = 0;
    game_over = 0;

    rand_state = rand_state + snake[0].x * 31337;

    place_food();

    draw_border();
    draw_score();
    draw_snake();
    draw_food();
}

// Check for collision
static int check_collision(int x, int y) {
    if (x < 0 || x >= BOARD_WIDTH || y < 0 || y >= BOARD_HEIGHT) {
        return 1;
    }
    for (int i = 1; i < snake_len; i++) {
        if (snake[i].x == x && snake[i].y == y) {
            return 1;
        }
    }
    return 0;
}

// Update game state
static void update_game(void) {
    direction = next_direction;

    point_t new_head = snake[0];
    switch (direction) {
        case DIR_UP:    new_head.y--; break;
        case DIR_DOWN:  new_head.y++; break;
        case DIR_LEFT:  new_head.x--; break;
        case DIR_RIGHT: new_head.x++; break;
    }

    if (check_collision(new_head.x, new_head.y)) {
        game_over = 1;
        return;
    }

    int ate_food = (new_head.x == food.x && new_head.y == food.y);

    if (!ate_food) {
        clear_cell(snake[snake_len - 1].x, snake[snake_len - 1].y);
    }

    if (ate_food && snake_len < MAX_SNAKE_LEN) {
        snake_len++;
    }
    for (int i = snake_len - 1; i > 0; i--) {
        snake[i] = snake[i - 1];
    }
    snake[0] = new_head;

    if (snake_len > 1) {
        draw_cell(snake[1].x, snake[1].y, 'o', COLOR_GREEN);
    }
    draw_cell(snake[0].x, snake[0].y, '@', COLOR_GREEN);

    if (ate_food) {
        score += 10;
        if (score > high_score) {
            high_score = score;
        }
        draw_score();
        place_food();
        draw_food();
    }
}

// Process input
static int process_input(void) {
    while (api->has_key()) {
        int c = api->getc();

        switch (c) {
            case 'q':
            case 'Q':
                return -1;

            case 'w':
            case 'W':
                if (direction != DIR_DOWN) next_direction = DIR_UP;
                break;
            case 's':
            case 'S':
                if (direction != DIR_UP) next_direction = DIR_DOWN;
                break;
            case 'a':
            case 'A':
                if (direction != DIR_RIGHT) next_direction = DIR_LEFT;
                break;
            case 'd':
            case 'D':
                if (direction != DIR_LEFT) next_direction = DIR_RIGHT;
                break;
        }
    }
    return 0;
}

// Show game over
static void show_game_over(void) {
    int center_y = BOARD_Y + BOARD_HEIGHT / 2;
    int center_x = BOARD_X + BOARD_WIDTH / 2 - 5;

    api->set_cursor(center_y - 1, center_x - 2);
    api->set_color(COLOR_RED, COLOR_BLACK);
    api->puts("  GAME OVER!  ");

    api->set_cursor(center_y + 1, center_x - 2);
    api->set_color(COLOR_WHITE, COLOR_BLACK);
    api->puts("  Score: ");
    api->print_int(score);
    api->puts("  ");

    api->set_cursor(center_y + 3, center_x - 4);
    api->set_color(COLOR_AMBER, COLOR_BLACK);
    api->puts("[R]estart  [Q]uit");
    api->set_color(COLOR_WHITE, COLOR_BLACK);
}

// Wait for restart or quit
static int wait_for_restart(void) {
    while (1) {
        if (api->has_key()) {
            int c = api->getc();
            if (c == 'r' || c == 'R') {
                return 1;
            }
            if (c == 'q' || c == 'Q') {
                return 0;
            }
        }
        api->sleep_ms(10);
    }
}

// Main entry point
int main(kapi_t *kapi, int argc, char **argv) {
    (void)argc;
    (void)argv;

    api = kapi;

    // Disable text cursor for game
    api->set_cursor_enabled(0);

    init_game();

    while (1) {
        if (process_input() < 0) {
            break;
        }

        update_game();

        if (game_over) {
            show_game_over();
            if (wait_for_restart()) {
                init_game();
                continue;
            } else {
                break;
            }
        }

        // Game tick - sleep controls game speed
        api->sleep_ms(SNAKE_DELAY_MS);
    }

    api->clear();
    api->set_color(COLOR_WHITE, COLOR_BLACK);
    api->set_cursor_enabled(1);  // Re-enable cursor

    return score;
}
