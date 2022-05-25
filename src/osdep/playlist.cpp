/*
 * UAE - Redquark Playlist handling
 * 
 * (c) 2022 Chris Smith
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "playlist.h"
#include "disk.h"

static Playlist *main_playlist = NULL;

static unsigned char * find_entry( Playlist *pl, int item, int *len );
static int count_tree_levels( const unsigned char *p, int len );

// ----------------------------------------------------------------------------
//
Playlist *
playlist_open( const char *filename )
{
    Playlist *pl = (Playlist *)malloc( sizeof(Playlist) );
    if( pl == NULL ) return NULL;

    void *  mem;
    struct  stat sb;
    int     fd = -1;
    int     ret = 0;
    int     len = 0;

    if( (fd = open( (char*)filename, O_RDONLY ) ) < 0 ) ret = (-3);
    if( ret == 0 && fstat( fd, &sb ) < 0 ) ret = (-2);

    if( ret == 0 ) {
        if( (mem = mmap( (caddr_t)0, (int)sb.st_size, 
                        PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0 )) != MAP_FAILED ) {
            len = (int)sb.st_size;
        } else ret = (-1);
    }

    if( ret > (-3) ) close( fd );

    if( ret < 0 ) return NULL;

    pl->data = (unsigned char *)mem;
    pl->len  = len;

    int plen = strlen(filename);
    int i = plen - 1;
    while( i >= 0 && filename[i] != '/' ) i--;

    i++; // Take into account tail '/'
    pl->playlist_dir = strndup( filename, i ); // Note strdup will also byte for \0 terminator
    pl->playlist_dir_len = i;

    int count;
    find_entry( pl, -1, &count ); // Return count == 0 if no entries in file

    if( count == 0 ) {
        playlist_close( &pl ); // This will release pl and set to NULL
    } else {
        pl->count = count;
    }

    return pl;
}

// ----------------------------------------------------------------------------
//
void
playlist_close( Playlist ** pl )
{
    if( pl == NULL || *pl == NULL ) return;
    if( (*pl)->data ) munmap( (void *)(*pl)->data, (*pl)->len );
    if( (*pl)->playlist_dir ) free( (*pl)->playlist_dir );
    free( *pl );
    *pl = NULL;
}

// ----------------------------------------------------------------------------
// If item is < 0, a special mode is entered where the number of filenames in the playlist
// is returned in len, return WILL be NULL in this case
//
static unsigned char *
find_entry( Playlist *pl, int item, int *len )
{
    unsigned char *p = pl->data;

    int count = 0;

    unsigned char *s;
    unsigned char *e = p + pl->len;

    while( p < e && (item < 0 || count <= item ) ) {

        // Find end of line
        while( *p == ' ') p++; // skip leading spaces

        if( *p == '#' ) while( *p >= ' ' ) p++; // Skip comment/extensino
        // Skip blank lines (or comment line ending)
        if( *p == '\r' ) p++;
        if( *p == '\n' ) {
            p++;
            continue;
        }

        // Find end of line
        s = p;
        while( *p >= ' ') p++; // skip up to line ending
        *len = p - s;

        while( *p <  ' ' && *p > 0 ) p++; // Eat any line endings

        count++;
    }

    if( item < 0 ) *len = count;       // Special case -return number of entries
    else if( count <= item ) *len = 0; // Usual case, return string length
    else {
        // Discard trailing spaces
        while( (*len) && s[(*len) - 1] == ' ' ) (*len)--;
    }

    return count > item ? s : NULL;
}

// ----------------------------------------------------------------------------
//
int
playlist_get_count( Playlist * pl )
{
    if( pl == NULL ) return 0;
    return pl->count;
}

// ----------------------------------------------------------------------------
//
unsigned char *
playlist_get_entry( Playlist *pl, int item, int *len )
{
    if( item < 0 || item >= pl->count ) return NULL;

    int flen;
    unsigned char *f =  find_entry( pl, item, &flen );

    int tl = pl->playlist_dir_len + flen;
    unsigned char *b = (unsigned char *)malloc( tl + 1 );

    memcpy( b, pl->playlist_dir, pl->playlist_dir_len );
    memcpy( b + pl->playlist_dir_len, f, flen );
    b[tl] = '\0';

    if( len != NULL ) *len = tl;

    return b;
}

// ----------------------------------------------------------------------------
//
int
playlist_insert_disk( int disk_number )
{
    if( main_playlist == NULL ) return -1;
    if( disk_number < 1 || disk_number > playlist_get_count( main_playlist ) ) return -1;

    if( currprefs.playlist_current_disk == disk_number ) return 0;

    unsigned char *df = playlist_get_entry( main_playlist, disk_number - 1, NULL );

#ifdef CPU_AMD64
    printf("Insert disk %d [%s]\n", disk_number, df );
#endif

    disk_insert(0, (char *)df );
    free( df );

    currprefs.playlist_current_disk = disk_number;

    return 0;
}

// ----------------------------------------------------------------------------
//
int
playlist_loaded_count( )
{
    if( main_playlist == NULL ) return 0;
    return playlist_get_count( main_playlist );

    return 0;
}

// ----------------------------------------------------------------------------
//
int
playlist_auto_open( struct uae_prefs* prefs, char *filepath )
{
#ifdef CPU_AMD64
    printf("Process playlist line [%s]\n", filepath );
#endif

    int i = strlen(filepath) - 1;
    while( i > 0 && filepath[i] >= '0' && filepath[i] <= '9' && filepath[i] != ':' ) i--;

    int cd = ( filepath[i] == ':' ) ? atoi( filepath + i + 1) : 0; // Default of 0 means no-disk

    if( filepath[i] == ':' ) filepath[i] = '\0';

#ifdef CPU_AMD64
    printf("Open playlist [%s]\n", filepath );
#endif

    if( main_playlist != NULL ) playlist_close( &main_playlist );

    main_playlist = playlist_open( filepath );
    if( main_playlist == NULL ) return -1;

    if( playlist_get_count( main_playlist ) < 1 ) {
        playlist_close( &main_playlist );
        return -1;
    }

#ifdef CPU_AMD64
    printf("Set current disk %d\n", cd );
#endif

    if( strcmp( prefs->playlist, filepath ) != 0 ) strcpy( prefs->playlist, filepath );
    prefs->playlist_current_disk = cd;

    return 0;
}

// ----------------------------------------------------------------------------
//
void
playlist_auto_prefs(struct uae_prefs* prefs, char* filepath)
{
    if( playlist_auto_open( prefs, filepath ) < 0 ) return;

    playlist_insert_disk( 1 );

	prefs->start_gui = false;
}

// ----------------------------------------------------------------------------
// end
