/* karabas: no textscreen library: the textscreen calls of chocolate-doom 2.3.0 src/heretic/d_main.c
   (the startup screen). TXT_Init fails, so Heretic takes the plain text startup. */
#pragma once
enum { TXT_COLOR_BLACK, TXT_COLOR_BLUE, TXT_COLOR_GREEN, TXT_COLOR_CYAN, TXT_COLOR_RED, TXT_COLOR_MAGENTA,
       TXT_COLOR_BROWN, TXT_COLOR_GREY, TXT_COLOR_DARK_GREY, TXT_COLOR_BRIGHT_BLUE, TXT_COLOR_BRIGHT_GREEN,
       TXT_COLOR_BRIGHT_CYAN, TXT_COLOR_BRIGHT_RED, TXT_COLOR_BRIGHT_MAGENTA, TXT_COLOR_YELLOW,
       TXT_COLOR_BRIGHT_WHITE };
#define TXT_Init() 0
#define TXT_Shutdown() ((void) 0)
#define TXT_GetScreenData() ((unsigned char *) 0)
#define TXT_UpdateScreen() ((void) 0)
#define TXT_GetChar() 0
