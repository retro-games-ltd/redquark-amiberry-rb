/*
 * UAE - Redquark Disk Swap UI
 * 
 * (c) 2021 Chris Smith
 */
#ifdef REDQUARK
#include "virtual_keyboard.h"
#include "disk_swap.h"
#include "amiberry_filesys.hpp"
#include "custom.h"
#include "playlist.h"

#include "malifb.h"

//#define DISK_SWAP_DISABLED 1

#define TIME_NOW_MS (read_processor_time()/1000)

#define DF_W 290
#define DF_H 46

typedef struct sprite {
    int x;
    int y;
    int w;
    int h;
} Sprite;

typedef enum {
    Sprite_Floppy,
    Sprite_TopLeft,
    Sprite_BottomLeft,
    Sprite_TopRight,
    Sprite_BottomRight,
    Sprite_Left,
    Sprite_Top,
    Sprite_Right,
    Sprite_Bottom,
    Sprite_Center,
    Sprite_D0,
    Sprite_D1,
    Sprite_D2,
    Sprite_D3,
    Sprite_D4,
    Sprite_D5,
    Sprite_D6,
    Sprite_D7,
    Sprite_D8,
    Sprite_D9,
    Sprite_DS,
    Sprite_MAX,
} SpriteID;

static Sprite Sprites[ Sprite_MAX ] = {
    {   1,  1, 44, 44 }, // Floppy disk
    {  49, 32,  6,  6 }, // Top    Left
    {  49, 39,  6,  6 }, // Bottom Left
    {  57, 32,  6,  6 }, // Top    Right
    {  57, 39,  6,  6 }, // Bottom Right
    {  64, 32,  6,  6 }, // Left
    {  64, 39,  6,  6 }, // Top
    {  72, 32,  6,  6 }, // Right
    {  72, 39,  6,  6 }, // Bottom
    {  79, 32,  6,  6 }, // Center
    {  48,  2, 20, 28 }, // Digit 0
    {  71,  2, 20, 28 }, // Digit 1
    {  91,  2, 20, 28 }, // Digit 2
    { 113,  2, 20, 28 }, // Digit 3
    { 135,  2, 20, 28 }, // Digit 4
    { 158,  2, 20, 28 }, // Digit 5
    { 179,  2, 20, 28 }, // Digit 6
    { 201,  2, 20, 28 }, // Digit 7
    { 224,  2, 20, 28 }, // Digit 8
    { 245,  2, 20, 28 }, // Digit 9
    { 268,  2, 20, 28 }, // Digit /
};

extern int get_host_hz(); // amiberry_gfx.cpp
extern int savestate_then_quit;

static int is_enabled = 0;
static int is_disabling = 0;

static Delta       delta_fade;
static DelayPeriod delay_show;

static MFB_Texture *disk_swap_texture = NULL;
static int disk_count = 3;
static int disk_current = 1;

typedef enum { // Must be in clockwise order from bottom left
    FSurface_BottomLeft,
    FSurface_Left,
    FSurface_TopLeft,
    FSurface_Top,
    FSurface_TopRight,
    FSurface_Right,
    FSurface_BottomRight,
    FSurface_Bottom,
    FSurface_Center,
    FSurface_MAX,
} FrameSurface;

typedef struct {
    int w;  // Sprite will be stretched to fit these bounds, if 0 then actual size used
    int h;
    int dx; // Position with repsect to last sprite. -1 = left/below, +1 = right/above
    int dy; //
    SpriteID id;
} FrameSection;

static FrameSection FrameParts[FSurface_MAX] = {
    {   0,   0,  0,  0, Sprite_BottomLeft },
    {   0,  80,  0,  1, Sprite_Left },
    {   0,   0,  0,  1, Sprite_TopLeft },
    { 300,   0,  1,  0, Sprite_Top },
    {   0,   0,  1,  0, Sprite_TopRight },
    {   0,  80,  0, -1, Sprite_Right },
    {   0,   0,  0, -1, Sprite_BottomRight },
    { 300,   0, -1,  0, Sprite_Bottom },
    { 300,  80,  0,  1, Sprite_Center },
};

static MFB_Surface *frame_surfaces[FSurface_MAX ] = {NULL};

typedef enum {
    ISurface_Disk,
    ISurface_CountDigit1,
    ISurface_CountDigit2,
    ISurface_CountSep,
    ISurface_MaxDigit1,
    ISurface_MaxDigit2,
    ISurface_MAX,
} IconSurface;

static MFB_Surface *icon_surfaces[ ISurface_MAX ] = {NULL};
static int disk_swap_update_meta( int current, int max );

// ----------------------------------------------------------------------------
//
static int
build_frame( MFB_Screen *screen )
{
    FrameSection *fs;
    Sprite       *sp;
    int x = 1280 / 2 - 312 / 2;
    int y = 720 / 2 - 92 / 2;
    int h = 0;
    int w = 0;
    int ah, aw;
    int i;
    for(i = FSurface_BottomLeft; i < FSurface_MAX; i++ ) {
        fs = &FrameParts[i];
        sp = &Sprites[ fs->id ];

        aw = fs->w ? fs->w : sp->w;
        ah = fs->h ? fs->h : sp->h;

        // h and w are here the last surface size

        if( fs->dx ) {
            if( fs->dx > 0 ) // To the right of the last sprite
                x += w;
            if( fs->dx < 0 ) // tp the left of the last sprite
                x -= aw;
        }
        if( fs->dy ) {
            if( fs->dy > 0 ) // Above the last sprite
                y += h;
            if( fs->dy < 0 ) // Below the last sprite
                y -= ah;
        }
        
        w = aw;
        h = ah;

        //printf("%d, %d, %d, %d\n", x, y, w, h );

        MFB_Surface *sf = MFB_SurfaceCreate( screen, x, y, w, h, 1, MFB_Surface_Opaque | MFB_Surface_Hidden );

        //printf(" Update surface with texture coords %d %d %d %d\n", sp->x, sp->y, sp->w, sp->h );
        if( MFB_SurfaceUpdate( sf, sp->x, sp->y, sp->w, sp->h, disk_swap_texture ) != 0 ) {
            printf("ERROR\n");
        }

        frame_surfaces[ i ] = sf;
    }
     
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
disk_swap_init( MFB_Screen *screen )
{
    int ret = -1;
    unsigned char *vkimg = NULL;

    int x,y;

#ifdef DISK_SWAP_DISABLED
    return 1; // Pretend init was okay
#endif

    disk_swap_finish( ); // Clean up

    disk_current = currprefs.playlist_current_disk;
    disk_count = playlist_loaded_count();

    if( disk_count == 0 ) return 0;

    if( disk_current < 1 || disk_current > disk_count ) disk_current = 1;
    

    do {
        if( (vkimg = load_image( "data/disk_flip.rgba", DF_W, DF_H )) == NULL ) {
            break;
        }

        disk_swap_texture = MFB_TextureCreate( DF_W, DF_H, MFB_RGBA );
        if( MFB_TextureRegister( screen, disk_swap_texture ) < 0 ) {
            break;
        }

        if( MFB_TextureUpdate( disk_swap_texture, 0, 0, disk_swap_texture->width, disk_swap_texture->height, vkimg, MFB_Texture_Flag_None ) != 0 ) {
            break;
        }

        build_frame( screen );

        int i;
        for( i = ISurface_Disk; i < ISurface_MAX; i++ ) {
            icon_surfaces[ i ] = MFB_SurfaceCreate( screen, 0, 0, 0, 0, 2, MFB_Surface_Transparent | MFB_Surface_Hidden);
        }
        
        ret = 0;
    } while( 0 );

    // No need for image data any more, as it has been transfered to the GPU
    if( vkimg ) free( vkimg );
    vkimg = NULL;

    if( ret != 0 ) {
        if( disk_swap_texture ) MFB_TextureDestroy( &disk_swap_texture );

        // FIXME Clearn up all frame and icon surfaces
        //if( disk_surface      ) MFB_SurfaceDestroy( &disk_surface    );
        return ret;
    }

    delta_initialise( &delta_fade, 0, 500/* fade ms */, DELTA_FLAG_LOG_INV );
    delay_period_cancel( &delay_show );

    return ret;
}

// ----------------------------------------------------------------------------
//
static int
disk_swap_set_meta_and_show( int current, int max )
{
    if( current > 99 || max > 99 ) return -1;

    int dc_ten  = Sprite_D0 + current / 10;
    int dc_unit = Sprite_D0 + current % 10;
    int dm_ten  = Sprite_D0 + max / 10;
    int dm_unit = Sprite_D0 + max % 10;

    // Calculate total width of display
    //
    int total_w = Sprites[ Sprite_Floppy ].w + Sprites[ dc_unit ].w + Sprites[ Sprite_DS ].w + Sprites[ dm_unit ].w;
    total_w += dc_ten > Sprite_D0 ? Sprites[ dc_ten ].w : 0;
    total_w += dm_ten > Sprite_D0 ? Sprites[ dm_ten ].w : 0;
    total_w += 20; // Padding between disk and text

    int x = 1280 / 2 - total_w / 2;
    int cy = 720 / 2;

    int i;
    int pad;
    int target = ISurface_Disk;
    int sid;
    Sprite *s;
    for( i = ISurface_Disk; i < ISurface_MAX; i++ ) {
        pad = 0;
        switch(i) {
            case ISurface_Disk:        sid = Sprite_Floppy; pad = 20; break;
            case ISurface_CountSep:    sid = Sprite_DS;     break;
            case ISurface_CountDigit1: sid = dc_ten; if( sid == Sprite_D0 ) continue; break;
            case ISurface_MaxDigit1:   sid = dm_ten; if( sid == Sprite_D0 ) continue; break;
            case ISurface_CountDigit2: sid = dc_unit; break;
            case ISurface_MaxDigit2:   sid = dm_unit; break;
            default: continue;
        }

        Sprite *s = &Sprites[ sid ];
        MFB_SurfaceSize  ( icon_surfaces[ target ], x, cy - s->h / 2, s->w, s->h );
        MFB_SurfaceUpdate( icon_surfaces[ target ], s->x, s->y, s->w, s->h, disk_swap_texture );
        MFB_SurfaceReveal( icon_surfaces[ target ] );
        x += s->w + pad;

        target++;
    }

    for( ; target < ISurface_MAX; target++ ) {
        MFB_SurfaceHide( icon_surfaces[ target ] );
    }

    // Show the frame too
    for(i = FSurface_BottomLeft; i < FSurface_MAX; i++ ) {
        MFB_SurfaceReveal( frame_surfaces[ i ] );
    }

    return 0;
}

// ----------------------------------------------------------------------------
//
static void
disk_swap_hide()
{
    int i;
    for( i = ISurface_Disk; i < ISurface_MAX; i++ ) {
        MFB_SurfaceHide( icon_surfaces[ i ] );
    }

    for(i = FSurface_BottomLeft; i < FSurface_MAX; i++ ) {
        MFB_SurfaceHide( frame_surfaces[ i ] );
    }
}

// ----------------------------------------------------------------------------
// Will hide surfaces if alpha is zero
static int
disk_swap_set_alpha( int alpha )
{
    if( alpha == 0 ) disk_swap_hide();
    else {
        int i;
        for( i = ISurface_Disk; i < ISurface_MAX; i++ ) {
            MFB_SurfaceAlpha( icon_surfaces[ i ], alpha );
        }
        for(i = FSurface_BottomLeft; i < FSurface_MAX; i++ ) {
            MFB_SurfaceAlpha( frame_surfaces[ i ], alpha );
        }
    }
    return 0;
}

// ----------------------------------------------------------------------------
int
disk_swap_finish( )
{
    if( disk_swap_texture ) MFB_TextureDestroy( &disk_swap_texture );
    // FIXME Clearn up all frame and icon surfaces
    //if( disk_surface    ) MFB_SurfaceDestroy( &disk_surface    );

    return 0;
}

// ----------------------------------------------------------------------------
int
disk_swap_enable()
{
    int x,y;

#ifdef DISK_SWAP_DISABLED
    return 0;
#endif

    if( is_disabling || savestate_then_quit ) return 0;

    disk_swap_set_meta_and_show( disk_current, disk_count );

    delta_reset( &delta_fade, 100 ); // Force fade to 100 (on)
    delay_period_set  ( &delay_show, 500 );

    is_enabled = 1;

    disk_swap_process( );

    MFB_SurfaceReveal( icon_surfaces[ ISurface_Disk ] );

    return 0;
}

// ----------------------------------------------------------------------------
//
void
disk_swap_next()
{
#ifdef DISK_SWAP_DISABLED
    return;
#endif
    if( disk_count == 0 ) return;

    disk_current = 1 + disk_current % disk_count;
    disk_swap_enable();
}

// ----------------------------------------------------------------------------
//
void
disk_swap_previous()
{
#ifdef DISK_SWAP_DISABLED
    return;
#endif
    if( disk_count == 0 ) return;

    disk_current--;
    if( !disk_current ) disk_current = disk_count;

    disk_swap_enable();
}

// ----------------------------------------------------------------------------
//
int
disk_swap_disable()
{
    //if( is_disabling ) return 0;
#ifdef DISK_SWAP_DISABLED
    return 0;
#endif

    disk_swap_hide();

    is_enabled = 0;
    
    //is_disabling = 1;

    return 0;
}

// ----------------------------------------------------------------------------
// Returns true if move occurred
int
disk_swap_process( )
{
    if( !is_enabled ) return 0;

    int dr = delay_period_process( &delay_show );

    if( dr < 0 && delta_get( &delta_fade ) == 0.0f ) {
        disk_swap_disable();

        return 0;
    }

    if( dr == 0 ) {
        delay_period_cancel( &delay_show );
        delta_set( &delta_fade, 0, DELTA_FLAG_NONE );
        
        playlist_insert_disk( disk_current );

    } else if( dr < 0 ) {
        delta_process( &delta_fade );
    }

    disk_swap_set_alpha( delta_get( &delta_fade ) );

    return 0;
}

// ----------------------------------------------------------------------------
#endif //REDQUARK
