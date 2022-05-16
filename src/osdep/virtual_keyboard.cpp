/*
 * UAE - Redquark Virtual Keyboard
 * 
 * (c) 2021 Chris Smith
 */
#ifdef REDQUARK
#include "virtual_keyboard.h"
#include "amiberry_filesys.hpp"
#include "custom.h"

#include "malifb.h"

#define PERIOD_IN_FRAMES 6
#define REVEAL_IN_FRAMES 10
#define KEY_RELEASE_IN_FRAMES 4 // XXX THIS MUST BE LESS THAN PERIOD AND REVEAL_IN_FRAMES otherwise key may not be released before cirtual keyboard closes.

#define TIME_NOW_MS (read_processor_time()/1000)

#define KB_W 106
#define KB_H 642

#define SL_W 28
#define SL_H 28
#define SL_THICKNESS 3

#define SZ ((changed_prefs.gfx_dynamic_safe_zone * 2) + 2) // Adds a 2 pixel pad

#define NO_MOD -1
#define CAN_SF -2 // Can have shift applied

struct virtual_keyboard vkeyboard = {
  {
    {{ AK_A,        CAN_SF, }, { AK_B,        CAN_SF, }, { AK_C,          CAN_SF, }, { AK_D,          CAN_SF, }},
    {{ AK_E,        CAN_SF, }, { AK_F,        CAN_SF, }, { AK_G,          CAN_SF, }, { AK_H,          CAN_SF, }},
    {{ AK_I,        CAN_SF, }, { AK_J,        CAN_SF, }, { AK_K,          CAN_SF, }, { AK_L,          CAN_SF, }},
    {{ AK_M,        CAN_SF, }, { AK_N,        CAN_SF, }, { AK_O,          CAN_SF, }, { AK_P,          CAN_SF, }},
    {{ AK_Q,        CAN_SF, }, { AK_R,        CAN_SF, }, { AK_S,          CAN_SF, }, { AK_T,          CAN_SF, }},
    {{ AK_U,        CAN_SF, }, { AK_V,        CAN_SF, }, { AK_W,          CAN_SF, }, { AK_X,          CAN_SF, }},
    {{ AK_Y,        CAN_SF, }, { AK_Z,        CAN_SF, }, { AK_1,          NO_MOD, }, { AK_2,          NO_MOD, }},
    {{ AK_3,        NO_MOD, }, { AK_4,        NO_MOD, }, { AK_5,          NO_MOD, }, { AK_6,          NO_MOD, }},
    {{ AK_7,        NO_MOD, }, { AK_8,        NO_MOD, }, { AK_9,          NO_MOD, }, { AK_0,          NO_MOD, }},

    {{ AK_SPC,      NO_MOD, }, { AK_UP,       NO_MOD, }, { AK_RET,        NO_MOD, }, { AK_BS,         NO_MOD, }},
    {{ AK_LF,       NO_MOD, }, { AK_DN,       NO_MOD, }, { AK_RT,         NO_MOD, }, { AK_DEL,        NO_MOD, }},

    {{ AK_1,        AK_LSH, }, { AK_2,        AK_LSH, }, { AK_3,          AK_LSH, }, { AK_4,          AK_LSH, }}, 
    {{ AK_5,        AK_LSH, }, { AK_6,        AK_LSH, }, { AK_7,          AK_LSH, }, { AK_8,          AK_LSH, }},
    {{ AK_MINUS,    NO_MOD, }, { AK_MINUS,    AK_LSH, }, { AK_EQUAL,      NO_MOD, }, { AK_EQUAL,      AK_LSH, }},
    {{ AK_9,        AK_LSH, }, { AK_0,        AK_LSH, }, { AK_BACKSLASH,  NO_MOD, }, { AK_BACKSLASH,  AK_LSH, }},
    {{ AK_LBRACKET, NO_MOD, }, { AK_RBRACKET, NO_MOD, }, { AK_QUOTE,      NO_MOD, }, { AK_QUOTE,      AK_LSH, }},
    {{ AK_LBRACKET, AK_LSH, }, { AK_RBRACKET, AK_LSH, }, { AK_SEMICOLON,  NO_MOD, }, { AK_SEMICOLON,  AK_LSH, }},
    {{ AK_COMMA,    AK_LSH, }, { AK_PERIOD,   AK_LSH, }, { AK_COMMA,      NO_MOD, }, { AK_PERIOD,     NO_MOD, }},
    {{ AK_SLASH,    NO_MOD, }, { AK_SLASH,    AK_LSH, }, { AK_BACKQUOTE,  NO_MOD, }, { AK_BACKQUOTE,  AK_LSH, }},

    {{ AK_LSH,      NO_MOD, }, { AK_CTRL,     NO_MOD, }, { AK_LALT,       NO_MOD, }, { AK_RSH,        NO_MOD, }},
    {{ AK_CAPSLOCK, NO_MOD, }, { AK_LAMI,     NO_MOD, }, { AK_RAMI,       NO_MOD, }, { AK_TAB,        NO_MOD, }},

    {{ AK_F1,       NO_MOD, }, { AK_F2,       NO_MOD, }, { AK_F3,         NO_MOD, }, { AK_F4,         NO_MOD, }},
    {{ AK_F5,       NO_MOD, }, { AK_F6,       NO_MOD, }, { AK_F7,         NO_MOD, }, { AK_F8,         NO_MOD, }},
    {{ AK_F9,       NO_MOD, }, { AK_F10,      NO_MOD, }, { AK_ESC,        NO_MOD, }, { AK_HELP,       NO_MOD, }},
  },
  0, 0,
  VK_ROWS,
  VK_COLS,
  0,
  0,
  {0},
  {0}
};

extern int get_host_hz(); // amiberry_gfx.cpp
extern int savestate_then_quit;

static int is_enabled = 0;
static int is_disabling = 0;
static int guide_has_been_released = 0;

static MFB_Surface *keyboard_surface = NULL;
static MFB_Surface *selector_surface = NULL;

static MFB_Texture *keyboard_texture = NULL;
static MFB_Texture *selector_texture = NULL;

// ----------------------------------------------------------------------------
//
static int
get_grid_coords( int row, int col, int *x, int *y )
{
    struct virtual_keyboard_key *key;

    if( row >= VK_ROWS || col >= VK_COLS ) return -1;

    key = &vkeyboard.mapping[row][col];

    // +/-3 accounts for the 3px border around the VK graphic.
    //
    *x = vkeyboard.ox + 3 + col * (KEY_WIDTH  + KEY_SPACE_H) - SL_THICKNESS;
    *y = vkeyboard.oy - 3 - row * (KEY_HEIGHT + KEY_SPACE_V) - KEY_HEIGHT - SL_THICKNESS;

    // There are additinoal key space rows at several places, so account for those.
    if( row >  8 ) *y -= KEY_SPACE_V;
    if( row > 10 ) *y -= KEY_SPACE_V;
    if( row > 18 ) *y -= KEY_SPACE_V;
    if( row > 20 ) *y -= KEY_SPACE_V;

    return 0;
}

// ----------------------------------------------------------------------------
//
static unsigned char *
load_image( const char *filename, int w, int h )
{
    int ret = -1;

    int fd = open( prefix_with_application_directory_path( filename ).c_str(), O_RDONLY );
    if( fd < 0 ) return NULL;

    int size = w * h * 32;
    void *img = malloc( size );
    ret = read( fd, img, size );

    if( ret < 0 ) free(img);

    close(fd);

    return ret < 0 ? NULL : (unsigned char *)img;
}

// ----------------------------------------------------------------------------
//
int
virtual_keyboard_init( MFB_Screen *screen )
{
    int ret = -1;
    unsigned char *vkimg = NULL;

    int x,y;

    virtual_keyboard_finish( ); // Clean up

    do {
        if( (vkimg = load_image( "data/virtual_keyboard.rgba", KB_W, KB_H )) < 0 ) {
            break;
        }

        keyboard_texture = MFB_TextureCreate( KB_W, KB_H, MFB_RGBA );
        if( MFB_TextureRegister( screen, keyboard_texture ) < 0 ) {
            break;
        }

        if( MFB_TextureUpdate( keyboard_texture, 0, 0, keyboard_texture->width, keyboard_texture->height, vkimg, MFB_Texture_Flag_None ) != 0 ) {
            break;
        }

        int bl_x = 1280;
        int bl_y = (720 - KB_H) / 2;

        vkeyboard.ox = bl_x;
        vkeyboard.oy = bl_y + KB_H;

        keyboard_surface = MFB_SurfaceCreate( screen, bl_x, bl_y, KB_W, KB_H, 1, MFB_Surface_Opaque | MFB_Surface_Hidden );

        if( MFB_SurfaceUpdate( keyboard_surface, 0, 0, keyboard_texture->width, keyboard_texture->height, keyboard_texture ) != 0 ) {
            break;
        }

        // No need for image data any more, as it has been transfered to the GPU
        free(vkimg);
        vkimg = NULL;

        if( (vkimg = load_image( "data/vk_selector.rgba", SL_W, SL_H )) < 0 ) {
            break;
        }

        selector_texture = MFB_TextureCreate( SL_W, SL_H, MFB_RGBA );
        if( MFB_TextureRegister( screen, selector_texture ) < 0 ) {
            break;
        }

        if( MFB_TextureUpdate( selector_texture, 0, 0, selector_texture->width, selector_texture->height, vkimg, MFB_Texture_Flag_None ) != 0 ) {
            break;
        }

        get_grid_coords( vkeyboard.row, vkeyboard.col, &x, &y );

        selector_surface = MFB_SurfaceCreate( screen, x, y, SL_W, SL_H, 2, MFB_Surface_Transparent | MFB_Surface_Hidden );

        if( MFB_SurfaceUpdate( selector_surface, 0, 0, selector_texture->width, selector_texture->height, selector_texture ) != 0 ) {
            MFB_TextureDestroy( &selector_texture );
            break;
        }
        ret = 0;
    } while( 0 );

    if( vkimg ) free( vkimg );

    if( ret != 0 ) {
        if( keyboard_texture ) MFB_TextureDestroy( &keyboard_texture );
        if( keyboard_surface ) MFB_SurfaceDestroy( &keyboard_surface );
        if( selector_texture ) MFB_TextureDestroy( &selector_texture );
        if( selector_surface ) MFB_SurfaceDestroy( &selector_surface );
        return ret;
    }

    int ms_per_frame = (get_host_hz() == 50) ? 20 : 17;

    delta_initialise( &vkeyboard.sel_delta_x, x, PERIOD_IN_FRAMES * ms_per_frame, DELTA_FLAG_LOG );
    delta_initialise( &vkeyboard.sel_delta_y, y, PERIOD_IN_FRAMES * ms_per_frame, DELTA_FLAG_LOG );

    delta_initialise( &vkeyboard.kbd_delta_x, KB_W, REVEAL_IN_FRAMES * ms_per_frame, DELTA_FLAG_LOG );
    
    delta_initialise( &vkeyboard.release_delta, 0, KEY_RELEASE_IN_FRAMES * ms_per_frame, DELTA_FLAG_LINEAR );

    // Force delta to desired initial target
    delta_reset( &vkeyboard.sel_delta_x, x );
    delta_reset( &vkeyboard.sel_delta_y, y );

    delta_reset( &vkeyboard.kbd_delta_x, KB_W );

    vkeyboard.direction = SELECTOR_NONE;

    return ret;
}

// ----------------------------------------------------------------------------
int
virtual_keyboard_finish( )
{
    if( keyboard_texture ) MFB_TextureDestroy( &keyboard_texture );
    if( keyboard_surface ) MFB_SurfaceDestroy( &keyboard_surface );
    if( selector_texture ) MFB_TextureDestroy( &selector_texture );
    if( selector_surface ) MFB_SurfaceDestroy( &selector_surface );

    return 0;
}

// ----------------------------------------------------------------------------
int
virtual_keyboard_enable()
{
    int x,y;

    if( is_enabled || is_disabling || savestate_then_quit ) return 0;

    if( get_grid_coords( vkeyboard.row, vkeyboard.col, &x, &y ) != 0 ) return -1;

    // Re-initialise in case host_hz has changed.
    int ms_per_frame = (get_host_hz() == 50) ? 20 : 17;
    delta_initialise( &vkeyboard.sel_delta_x, x, PERIOD_IN_FRAMES * ms_per_frame, DELTA_FLAG_LOG );
    delta_initialise( &vkeyboard.sel_delta_y, y, PERIOD_IN_FRAMES * ms_per_frame, DELTA_FLAG_LOG );

    delta_initialise( &vkeyboard.kbd_delta_x, KB_W + SZ, REVEAL_IN_FRAMES * ms_per_frame, DELTA_FLAG_LOG );

    // Force delta to desired initial target
    delta_reset( &vkeyboard.sel_delta_x, x );
    delta_reset( &vkeyboard.sel_delta_y, y );

    delta_set( &vkeyboard.kbd_delta_x, 0, DELTA_FLAG_LOG );

    is_enabled = 1;

    virtual_keyboard_process( );

    MFB_SurfaceReveal( keyboard_surface );
    MFB_SurfaceReveal( selector_surface );

    // Make sure handle_input waits for the guide button to be released
    // before it subsequently checks for it being pressed
    guide_has_been_released = 0;

    return 0;
}

// ----------------------------------------------------------------------------
//
int
virtual_keyboard_disable()
{
    if( is_disabling ) return 0;

    delta_set( &vkeyboard.kbd_delta_x, KB_W + SZ, DELTA_FLAG_LINEAR ); //LOG_INV );
    
    is_disabling = 1;

    return 0;
}

// ----------------------------------------------------------------------------
//
int
virtual_keyboard_get_displacement()
{
    if( !is_enabled) return 0;

    return 1280 - vkeyboard.ox;
}

// ----------------------------------------------------------------------------
// Returns true if move occurred
int
virtual_keyboard_process( )
{
    int moved = 0;
    int x, y;

    if( vkeyboard.release_key ) {
        if( !delta_process( &vkeyboard.release_delta ) ) {

            inputdevice_do_keyboard( vkeyboard.release_key, 0);
            if( vkeyboard.release_modifier > NO_MOD ) inputdevice_do_keyboard(vkeyboard.release_modifier, 0);

            delta_reset( &vkeyboard.release_delta, 0 );
            vkeyboard.release_key = 0;
        }
    }

    if( !is_enabled) return 0;

    int kx = delta_process( &vkeyboard.kbd_delta_x );
    if( kx ) {
        int bl_x = 1280 - KB_W - SZ;

        int k_dx = delta_get( &vkeyboard.kbd_delta_x );
        vkeyboard.ox = bl_x + k_dx;

        MFB_SurfaceSize( keyboard_surface, vkeyboard.ox, vkeyboard.oy - KB_H, keyboard_surface->width, keyboard_surface->height );

        get_grid_coords( vkeyboard.row, vkeyboard.col, &x, &y );
        delta_reset( &vkeyboard.sel_delta_x, x );

        moved = 1;
    } else if ( is_disabling ) {
        MFB_SurfaceHide( keyboard_surface );
        MFB_SurfaceHide( selector_surface );
        is_enabled = 0;
        is_disabling = 0;
    }

    int dx = delta_process( &vkeyboard.sel_delta_x );
    int dy = delta_process( &vkeyboard.sel_delta_y );

    if( moved || dx || dy ) {
        MFB_SurfaceSize( selector_surface,
                delta_get( &vkeyboard.sel_delta_x ), delta_get( &vkeyboard.sel_delta_y ),
                selector_surface->width, selector_surface->height );
        moved = 1;
        vkeyboard.direction = SELECTOR_NONE;
    } 

    if( !moved && vkeyboard.direction != SELECTOR_NONE ) {
        switch( vkeyboard.direction ) {
            case SELECTOR_UP:
                vkeyboard.row--;
                if( vkeyboard.row < 0 ) vkeyboard.row += VK_ROWS;
                break;

            case SELECTOR_DOWN:
                vkeyboard.row++;
                if( vkeyboard.row >= VK_ROWS ) vkeyboard.row = 0;
                break;

            case SELECTOR_LEFT:
                vkeyboard.col--;
                if( vkeyboard.col < 0 ) vkeyboard.col += VK_COLS;
                break;

            case SELECTOR_RIGHT:
                vkeyboard.col++;
                if( vkeyboard.col >= VK_COLS ) vkeyboard.col = 0;
                break;

            case SELECTOR_SELECT:
                break;
        }

        get_grid_coords( vkeyboard.row, vkeyboard.col, &x, &y );

        delta_set( &vkeyboard.sel_delta_x, x, DELTA_FLAG_LOG );
        delta_set( &vkeyboard.sel_delta_y, y, DELTA_FLAG_LOG );
    }

    return moved;
}

// ----------------------------------------------------------------------------
static const auto upper_bound = 32767;

#define MOVE_ON_BUTTON( c, b, d ) \
    if( SDL_GameControllerGetButton( (c), (b) ) & 1 ) dir = (d)

#define MOVE_ON_AXIS( c, a, dn, dp ) { \
    val = SDL_GameControllerGetAxis( (c), (a) ); \
	deadzone = currprefs.input_joystick_deadzone * upper_bound / 100; \
	if (val < deadzone && val > -deadzone) val = 0; \
    if( val < 0 ) dir = (dn); \
    else if( val > 0 ) dir = (dp); }
     
int
virtual_keyboard_handle_input( SDL_GameController *ctrl, int joyid, int offset, int start_b_val )
{
    static int           active_joyid = -1;
    static unsigned long repeat_time  =  0;

    SelectorDirection dir = SELECTOR_NONE;
    int val, deadzone, local_shift_state;

    if( !is_enabled || is_disabling ) return 0;

    if( savestate_then_quit ) { // we're enabled but quiting. Close the VK
        virtual_keyboard_disable();
        return 0;
    }
    
    do {
        // Don't process inputs for other controllers if a controller is currently "active"
        if( active_joyid >= 0 && active_joyid != joyid ) break;

        MOVE_ON_BUTTON( ctrl, SDL_CONTROLLER_BUTTON_DPAD_UP,    SELECTOR_UP     );
        MOVE_ON_BUTTON( ctrl, SDL_CONTROLLER_BUTTON_DPAD_DOWN,  SELECTOR_DOWN   );
        MOVE_ON_BUTTON( ctrl, SDL_CONTROLLER_BUTTON_DPAD_LEFT,  SELECTOR_LEFT   );
        MOVE_ON_BUTTON( ctrl, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SELECTOR_RIGHT  );
        MOVE_ON_BUTTON( ctrl, SDL_CONTROLLER_BUTTON_A,          SELECTOR_SELECT );

        // TODO Shoulder left/right buttons to move up/down keyboard by section.y
        //MOVE_ON_BUTTON( ctrl, SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  SELECTOR_LEFT   );
        //MOVE_ON_BUTTON( ctrl, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SELECTOR_RIGHT  );

        local_shift_state  = SDL_GameControllerGetButton( (ctrl), (SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ) & 1;
        local_shift_state |= SDL_GameControllerGetButton( (ctrl), (SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ) & 1;

        MOVE_ON_AXIS  ( ctrl, SDL_CONTROLLER_AXIS_LEFTX,        SELECTOR_LEFT, SELECTOR_RIGHT );
        MOVE_ON_AXIS  ( ctrl, SDL_CONTROLLER_AXIS_LEFTY,        SELECTOR_UP,   SELECTOR_DOWN  );

        MOVE_ON_AXIS  ( ctrl, SDL_CONTROLLER_AXIS_RIGHTX,       SELECTOR_LEFT, SELECTOR_RIGHT );
        MOVE_ON_AXIS  ( ctrl, SDL_CONTROLLER_AXIS_RIGHTY,       SELECTOR_UP,   SELECTOR_DOWN  );

        MOVE_ON_BUTTON( ctrl, SDL_CONTROLLER_BUTTON_B,          SELECTOR_RETURN );
        MOVE_ON_BUTTON( ctrl, SDL_CONTROLLER_BUTTON_X,          SELECTOR_DELETE );
        MOVE_ON_BUTTON( ctrl, SDL_CONTROLLER_BUTTON_Y,          SELECTOR_SPACE  );

        MOVE_ON_BUTTON( ctrl, SDL_CONTROLLER_BUTTON_GUIDE,      SELECTOR_GUIDE  );
        MOVE_ON_BUTTON( ctrl, SDL_CONTROLLER_BUTTON_BACK,       SELECTOR_GUIDE  );

        // Monitor start button, so we can detect when all buttons are released, then we set guide_has_been_released
        MOVE_ON_BUTTON( ctrl, SDL_CONTROLLER_BUTTON_START,      SELECTOR_START  ); 

        // a button being held will set active_joyid for that controller, so 
        // on activation of the VK, a button may be held down. Wait for no buttons
        // to be held and then clear guide_has_been_released, which ensures that
        // a subsequent detection of GUIDE being pressed will mean "Close the VK"
        //
        if( active_joyid >= 0 && dir == SELECTOR_NONE ) guide_has_been_released = 1;

        static int ov = -1;

        if( dir != SELECTOR_NONE && active_joyid < 0 ) active_joyid = joyid;

        if( dir < SELECTOR_SELECT ) { // Any direction
            if( dir == SELECTOR_NONE ) {
                repeat_time = 0;
                active_joyid = -1;
            }
            else if( ov != dir ) {
                vkeyboard.direction = dir;
                repeat_time = TIME_NOW_MS + (20 * 20);
            }
            else if( ov == dir && (repeat_time && repeat_time <= TIME_NOW_MS ) ) {
                vkeyboard.direction = dir;
            }
        } else {
            if( ov != dir && vkeyboard.direction == SELECTOR_NONE && vkeyboard.release_key == 0 ) {

                switch( dir ) {
                    case SELECTOR_SELECT:
                        {
                            int modifier = vkeyboard.mapping[ vkeyboard.row ][ vkeyboard.col ].modifier;
                            int keycode  = vkeyboard.mapping[ vkeyboard.row ][ vkeyboard.col ].keycode;

                            if( keycode == AK_CAPSLOCK ) {
                                SDL_Event e = {};
                                e.type = SDL_KEYDOWN;
                                e.key.keysym.scancode = SDL_SCANCODE_CAPSLOCK;
                                SDL_PushEvent(&e);
                            } else {
                                if( modifier == CAN_SF && local_shift_state ) modifier = AK_LSH;
                                if( modifier > NO_MOD ) inputdevice_do_keyboard (modifier, 1);
                                inputdevice_do_keyboard (keycode, 1);

                                // Queue up release of key.
                                vkeyboard.release_key      = keycode;
                                vkeyboard.release_modifier = modifier;
                                delta_set( &vkeyboard.release_delta, 10, DELTA_FLAG_LINEAR );
                            }
                        } 
                        break;

                    case SELECTOR_RETURN:
                        inputdevice_do_keyboard (AK_RET, 1);
                        
                        vkeyboard.release_key      = AK_RET;
                        vkeyboard.release_modifier = 0;
                        delta_set( &vkeyboard.release_delta, 10, DELTA_FLAG_LINEAR );
                        break;

                    case SELECTOR_DELETE:
                        inputdevice_do_keyboard (AK_BS, 1);

                        vkeyboard.release_key      = AK_BS;
                        vkeyboard.release_modifier = 0;
                        delta_set( &vkeyboard.release_delta, 10, DELTA_FLAG_LINEAR );
                        break;

                    case SELECTOR_SPACE:
                        inputdevice_do_keyboard (AK_SPC, 1);
                        
                        vkeyboard.release_key      = AK_SPC;
                        vkeyboard.release_modifier = 0;
                        delta_set( &vkeyboard.release_delta, 10, DELTA_FLAG_LINEAR );
                        break;

                    case SELECTOR_GUIDE:
                        if( guide_has_been_released ) virtual_keyboard_disable(); 
                        break;

                    default:
                        break;

                }
            }
        }
        ov = dir;
    } while(0);

    // Handle quit
    //val = SDL_GameControllerGetButton( ctrl, SDL_CONTROLLER_BUTTON_START );
    val = start_b_val;
    setjoybuttonstate( offset, 15, val & 1 );

    return 1;
}

// ----------------------------------------------------------------------------
#endif //REDQUARK
