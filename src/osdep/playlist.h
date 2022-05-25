/*
 * UAE - Redquark Virtual Keyboard
 * 
 * (c) 2021 Chris Smith
 */
#pragma once 

#include "sysconfig.h"
#include "sysdeps.h"
#include <ctype.h>
#include "options.h"

typedef struct {
    unsigned char *data;
    int            len;
    int            count;
    char          *playlist_dir;
    int            playlist_dir_len;

} Playlist;

Playlist * playlist_open( const char *filename );
void       playlist_close( Playlist ** pl );
int        playlist_get_count( Playlist * pl );
int playlist_insert_disk( int disk_number );
int playlist_loaded_count( );

void playlist_auto_prefs(struct uae_prefs* prefs, char* filepath );
int  playlist_auto_open( struct uae_prefs* prefs, char *filepath );

unsigned char *     playlist_get_entry( Playlist *pl, int item, int *len );
