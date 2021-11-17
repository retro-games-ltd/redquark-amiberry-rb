#pragma once

#include "malifb.h"
#include "sysdeps.h"
#include "options.h"
#include "inputdevice.h"
#include "keyboard.h"

#define VK_ROWS 24
#define VK_COLS 4

#define KEY_WIDTH  22
#define KEY_HEIGHT 22
#define KEY_SPACE_H 4
#define KEY_SPACE_V 4

typedef enum {
    DELTA_FLAG_NONE      = 0,
    DELTA_FLAG_LOG       = 1<<0,
    DELTA_FLAG_LOG_INV   = 1<<1,
    DELTA_FLAG_LINEAR    = 1<<2,
    DELTA_FLAG_PRECISION = 1<<3,
} DeltaFlag;

typedef struct {
    int           start;
    int           target;          // The value to hit
    float         current;         // The current fractional value
    unsigned long target_t;        // The absolute time at which to hit the target value
    unsigned long period_ms;       // The number of ms within which to hit the target value.
    unsigned long frame_period_ms; // The smallest duration between process calls
    DeltaFlag     flag;
} Delta;

typedef enum {
    SELECTOR_NONE   = 0,
    SELECTOR_UP     = 1<<0,
    SELECTOR_DOWN   = 1<<1,
    SELECTOR_LEFT   = 1<<2,
    SELECTOR_RIGHT  = 1<<3,

    SELECTOR_SELECT = 1<<4,
    SELECTOR_GUIDE  = 1<<5,
    SELECTOR_RETURN = 1<<6,
    SELECTOR_DELETE = 1<<7,
    SELECTOR_SPACE  = 1<<8,
} SelectorDirection;

struct virtual_keyboard_key {
    int keycode;
    int modifier;
    //int x;
    //int y;
};

struct virtual_keyboard {
    struct virtual_keyboard_key mapping[VK_ROWS][VK_COLS];
    int ox;
    int oy;

    int rows;
    int cols;
    int row;
    int col;

    Delta kbd_delta_x;
    Delta sel_delta_x;
    Delta sel_delta_y;
    Delta release_delta;

    int release_key;
    int release_modifier;

    SelectorDirection direction;
};

int virtual_keyboard_init( MFB_Screen *screen );
int virtual_keyboard_finish(); 
int virtual_keyboard_process( );
int virtual_keyboard_handle_input( SDL_GameController *ctrl, int joyid, int offset );
int virtual_keyboard_enable();
int virtual_keyboard_get_displacement();
