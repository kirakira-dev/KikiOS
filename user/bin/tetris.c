/*
 * KikiOS Tetris (Userspace)
 *
 * Classic falling blocks game.
 * Controls: A/D to move, W to rotate, S to drop, Space for hard drop, Q to quit
 */

#include "kiki.h"

// Board dimensions
#define BOARD_WIDTH  10
#define BOARD_HEIGHT 20
#define BOARD_X      25
#define BOARD_Y      2

// Piece types
#define NUM_PIECES 7

// The 7 tetrominos (I, O, T, S, Z, J, L)
static const uint8_t pieces[NUM_PIECES][4][4][4] = {
    // I piece
    {
        {{0,0,0,0}, {1,1,1,1}, {0,0,0,0}, {0,0,0,0}},
        {{0,0,1,0}, {0,0,1,0}, {0,0,1,0}, {0,0,1,0}},
        {{0,0,0,0}, {0,0,0,0}, {1,1,1,1}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,1,0,0}},
    },
    // O piece
    {
        {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
    },
    // T piece
    {
        {{0,1,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,1,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0}},
    },
    // S piece
    {
        {{0,1,1,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,1,0}, {0,0,1,0}, {0,0,0,0}},
        {{0,0,0,0}, {0,1,1,0}, {1,1,0,0}, {0,0,0,0}},
        {{1,0,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0}},
    },
    // Z piece
    {
        {{1,1,0,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,0,1,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,0,0}, {0,1,1,0}, {0,0,0,0}},
        {{0,1,0,0}, {1,1,0,0}, {1,0,0,0}, {0,0,0,0}},
    },
    // J piece
    {
        {{1,0,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,1,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,1,0}, {0,0,1,0}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,0,0}, {1,1,0,0}, {0,0,0,0}},
    },
    // L piece
    {
        {{0,0,1,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,0,0}, {0,1,1,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,1,0}, {1,0,0,0}, {0,0,0,0}},
        {{1,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0}},
    },
};

// Piece colors
static const uint32_t piece_colors[NUM_PIECES] = {
    0x00FFFF,  // I - cyan
    0xFFFF00,  // O - yellow
    0xFF00FF,  // T - magenta
    0x00FF00,  // S - green
    0xFF0000,  // Z - red
    0x0000FF,  // J - blue
    0xFFA500,  // L - orange
};

// Game state
static kapi_t *api;
static uint8_t board[BOARD_HEIGHT][BOARD_WIDTH];
static uint32_t board_colors[BOARD_HEIGHT][BOARD_WIDTH];
static int current_piece;
static int current_rotation;
static int current_x;
static int current_y;
static int next_piece;
static int score;
static int lines;
static int level;
static int game_over;
static int high_score = 0;

// Random number generator
static uint32_t rand_state = 54321;

static uint32_t rand(void) {
    rand_state = rand_state * 1103515245 + 12345;
    return (rand_state >> 16) & 0x7FFF;
}

// Game speed delay in milliseconds
#define TETRIS_TICK_MS 16  // ~60fps

// Draw the border
static void draw_border(void) {
    api->set_color(COLOR_WHITE, COLOR_BLACK);

    // Top border
    api->set_cursor(BOARD_Y - 1, BOARD_X - 1);
    api->putc('+');
    for (int x = 0; x < BOARD_WIDTH * 2; x++) {
        api->putc('-');
    }
    api->putc('+');

    // Side borders
    for (int y = 0; y < BOARD_HEIGHT; y++) {
        api->set_cursor(BOARD_Y + y, BOARD_X - 1);
        api->putc('|');
        api->set_cursor(BOARD_Y + y, BOARD_X + BOARD_WIDTH * 2);
        api->putc('|');
    }

    // Bottom border
    api->set_cursor(BOARD_Y + BOARD_HEIGHT, BOARD_X - 1);
    api->putc('+');
    for (int x = 0; x < BOARD_WIDTH * 2; x++) {
        api->putc('-');
    }
    api->putc('+');
}

// Draw the info panel
static void draw_info(void) {
    int info_x = BOARD_X + BOARD_WIDTH * 2 + 4;

    api->set_cursor(BOARD_Y, info_x);
    api->set_color(COLOR_AMBER, COLOR_BLACK);
    api->puts("TETRIS");

    api->set_cursor(BOARD_Y + 2, info_x);
    api->set_color(COLOR_WHITE, COLOR_BLACK);
    api->puts("Score: ");
    api->print_int(score);
    api->puts("    ");

    api->set_cursor(BOARD_Y + 3, info_x);
    api->puts("Lines: ");
    api->print_int(lines);
    api->puts("    ");

    api->set_cursor(BOARD_Y + 4, info_x);
    api->puts("Level: ");
    api->print_int(level);
    api->puts("    ");

    api->set_cursor(BOARD_Y + 5, info_x);
    api->puts("High:  ");
    api->print_int(high_score);
    api->puts("    ");

    api->set_cursor(BOARD_Y + 7, info_x);
    api->puts("Next:");

    // Draw next piece
    for (int y = 0; y < 4; y++) {
        api->set_cursor(BOARD_Y + 8 + y, info_x);
        for (int x = 0; x < 4; x++) {
            if (pieces[next_piece][0][y][x]) {
                api->set_color(piece_colors[next_piece], COLOR_BLACK);
                api->puts("[]");
            } else {
                api->puts("  ");
            }
        }
    }

    api->set_cursor(BOARD_Y + 14, info_x);
    api->set_color(COLOR_WHITE, COLOR_BLACK);
    api->puts("Controls:");

    api->set_cursor(BOARD_Y + 15, info_x);
    api->puts("A/D  Move");

    api->set_cursor(BOARD_Y + 16, info_x);
    api->puts("W    Rotate");

    api->set_cursor(BOARD_Y + 17, info_x);
    api->puts("S    Drop");

    api->set_cursor(BOARD_Y + 18, info_x);
    api->puts("Q    Quit");
}

// Draw a cell
static void draw_cell(int x, int y, uint32_t color) {
    api->set_cursor(BOARD_Y + y, BOARD_X + x * 2);
    if (color != COLOR_BLACK) {
        api->set_color(color, COLOR_BLACK);
        api->puts("[]");
    } else {
        api->set_color(COLOR_BLACK, COLOR_BLACK);
        api->puts("  ");
    }
}

// Draw the board
static void draw_board(void) {
    for (int y = 0; y < BOARD_HEIGHT; y++) {
        for (int x = 0; x < BOARD_WIDTH; x++) {
            draw_cell(x, y, board_colors[y][x]);
        }
    }
}

// Draw the current piece
static void draw_piece(int clear) {
    uint32_t color = clear ? COLOR_BLACK : piece_colors[current_piece];

    for (int py = 0; py < 4; py++) {
        for (int px = 0; px < 4; px++) {
            if (pieces[current_piece][current_rotation][py][px]) {
                int bx = current_x + px;
                int by = current_y + py;
                if (bx >= 0 && bx < BOARD_WIDTH && by >= 0 && by < BOARD_HEIGHT) {
                    draw_cell(bx, by, color);
                }
            }
        }
    }
}

// Check if piece can be placed
static int can_place(int piece, int rotation, int x, int y) {
    for (int py = 0; py < 4; py++) {
        for (int px = 0; px < 4; px++) {
            if (pieces[piece][rotation][py][px]) {
                int bx = x + px;
                int by = y + py;

                if (bx < 0 || bx >= BOARD_WIDTH || by >= BOARD_HEIGHT) {
                    return 0;
                }

                if (by >= 0 && board[by][bx]) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

// Lock piece in place
static void lock_piece(void) {
    for (int py = 0; py < 4; py++) {
        for (int px = 0; px < 4; px++) {
            if (pieces[current_piece][current_rotation][py][px]) {
                int bx = current_x + px;
                int by = current_y + py;
                if (bx >= 0 && bx < BOARD_WIDTH && by >= 0 && by < BOARD_HEIGHT) {
                    board[by][bx] = 1;
                    board_colors[by][bx] = piece_colors[current_piece];
                }
            }
        }
    }
}

// Clear completed lines
static int clear_lines_fn(void) {
    int cleared = 0;

    for (int y = BOARD_HEIGHT - 1; y >= 0; y--) {
        int complete = 1;
        for (int x = 0; x < BOARD_WIDTH; x++) {
            if (!board[y][x]) {
                complete = 0;
                break;
            }
        }

        if (complete) {
            cleared++;

            for (int yy = y; yy > 0; yy--) {
                for (int x = 0; x < BOARD_WIDTH; x++) {
                    board[yy][x] = board[yy - 1][x];
                    board_colors[yy][x] = board_colors[yy - 1][x];
                }
            }

            for (int x = 0; x < BOARD_WIDTH; x++) {
                board[0][x] = 0;
                board_colors[0][x] = COLOR_BLACK;
            }

            y++;
        }
    }

    return cleared;
}

// Spawn a new piece
static void spawn_piece(void) {
    current_piece = next_piece;
    next_piece = rand() % NUM_PIECES;
    current_rotation = 0;
    current_x = BOARD_WIDTH / 2 - 2;
    current_y = -1;

    if (!can_place(current_piece, current_rotation, current_x, current_y)) {
        game_over = 1;
    }
}

// Initialize game
static void init_game(void) {
    api->clear();

    for (int y = 0; y < BOARD_HEIGHT; y++) {
        for (int x = 0; x < BOARD_WIDTH; x++) {
            board[y][x] = 0;
            board_colors[y][x] = COLOR_BLACK;
        }
    }

    score = 0;
    lines = 0;
    level = 1;
    game_over = 0;

    next_piece = rand() % NUM_PIECES;
    spawn_piece();

    draw_border();
    draw_board();
    draw_info();
    draw_piece(0);
}

// Process input
static int process_input(void) {
    while (api->has_key()) {
        int c = api->getc();

        switch (c) {
            case 'q':
            case 'Q':
                return -1;

            case 'a':
            case 'A':
                if (can_place(current_piece, current_rotation, current_x - 1, current_y)) {
                    draw_piece(1);
                    current_x--;
                    draw_piece(0);
                }
                break;

            case 'd':
            case 'D':
                if (can_place(current_piece, current_rotation, current_x + 1, current_y)) {
                    draw_piece(1);
                    current_x++;
                    draw_piece(0);
                }
                break;

            case 'w':
            case 'W':
                {
                    int new_rot = (current_rotation + 1) % 4;
                    if (can_place(current_piece, new_rot, current_x, current_y)) {
                        draw_piece(1);
                        current_rotation = new_rot;
                        draw_piece(0);
                    }
                }
                break;

            case 's':
            case 'S':
                if (can_place(current_piece, current_rotation, current_x, current_y + 1)) {
                    draw_piece(1);
                    current_y++;
                    draw_piece(0);
                    score += 1;
                }
                break;

            case ' ':
                draw_piece(1);
                while (can_place(current_piece, current_rotation, current_x, current_y + 1)) {
                    current_y++;
                    score += 2;
                }
                draw_piece(0);
                break;
        }
    }
    return 0;
}

// Update game state
static void update_game(void) {
    if (can_place(current_piece, current_rotation, current_x, current_y + 1)) {
        draw_piece(1);
        current_y++;
        draw_piece(0);
    } else {
        lock_piece();

        int cleared = clear_lines_fn();
        if (cleared > 0) {
            lines += cleared;

            int points[] = {0, 100, 300, 500, 800};
            score += points[cleared] * level;

            level = (lines / 10) + 1;
            if (level > 10) level = 10;

            draw_board();
        }

        if (score > high_score) {
            high_score = score;
        }

        draw_info();

        spawn_piece();
        if (!game_over) {
            draw_piece(0);
        }
    }
}

// Show game over
static void show_game_over(void) {
    int center_y = BOARD_Y + BOARD_HEIGHT / 2;
    int center_x = BOARD_X + BOARD_WIDTH - 4;

    api->set_cursor(center_y - 1, center_x - 2);
    api->set_color(COLOR_RED, COLOR_BLACK);
    api->puts(" GAME OVER! ");

    api->set_cursor(center_y + 1, center_x - 2);
    api->set_color(COLOR_WHITE, COLOR_BLACK);
    api->puts(" Score: ");
    api->print_int(score);
    api->puts(" ");

    api->set_cursor(center_y + 3, center_x - 4);
    api->set_color(COLOR_AMBER, COLOR_BLACK);
    api->puts("[R]estart [Q]uit");
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

    int drop_counter = 0;
    int drop_speed = 18;  // Ticks between drops (at 60fps, 18 ticks = ~300ms)

    while (1) {
        if (process_input() < 0) {
            break;
        }

        drop_counter++;
        if (drop_counter >= drop_speed) {
            drop_counter = 0;
            update_game();

            // Speed curve: starts at 18, decreases by 2 per level
            // Level 1: 18 ticks (~300ms), Level 10: 4 ticks (~67ms)
            drop_speed = 20 - level * 2;
            if (drop_speed < 4) drop_speed = 4;
        }

        if (game_over) {
            show_game_over();
            if (wait_for_restart()) {
                init_game();
                drop_counter = 0;
                continue;
            } else {
                break;
            }
        }

        // Game tick - sleep controls game speed
        api->sleep_ms(TETRIS_TICK_MS);
    }

    api->clear();
    api->set_color(COLOR_WHITE, COLOR_BLACK);
    api->set_cursor_enabled(1);  // Re-enable cursor

    return score;
}
