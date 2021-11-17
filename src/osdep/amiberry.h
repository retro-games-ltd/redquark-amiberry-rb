#pragma once

#if defined REDQUARK

extern int savestate_then_quit;
extern int delay_savestate_frame;
extern char* screenshot_filename;

int copyfile( const char *target, const char *source, int replace );

#endif // REDQUARK
