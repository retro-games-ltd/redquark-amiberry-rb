#pragma once

#include "malifb.h"
#include "sysdeps.h"
#include "options.h"

int disk_swap_init( MFB_Screen *screen );
int disk_swap_finish(); 
int disk_swap_process( );
int disk_swap_enable();
void disk_swap_next();
void disk_swap_previous();
