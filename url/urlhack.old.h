/*
 * HACK: PuttyTray / Nutty
 * Hyperlink stuff: CORE FILE! Don't forget to COPY IT TO THE NEXT VERSION
 */
#ifndef _URLHACK_H
#define _URLHACK_H

#include "re_lib/regexp.h"

typedef struct { int x0, y0, x1, y1; } text_region;

const char* urlhack_default_regex;
const char* urlhack_liberal_regex;

enum {
    URLHACK_REGEX_CUSTOM = 0,
    URLHACK_REGEX_CLASSIC = 1,
    URLHACK_REGEX_LIBERAL,
};

int urlhack_mouse_old_x, urlhack_mouse_old_y, urlhack_current_region;

void urlhack_reset();
void urlhack_go_find_me_some_hyperlinks(int screen_width);
void urlhack_putchar(char ch);
text_region urlhack_get_link_region(int index);

int urlhack_is_in_link_region(int x, int y);
int urlhack_is_in_this_link_region(text_region r, int x, int y);
text_region urlhack_get_link_bounds(int x, int y);
void urlhack_add_link_region(int x0, int y0, int x1, int y1);
void urlhack_launch_url(const char* app, const char *url);
int urlhack_is_ctrl_pressed();
//void urlhack_set_regular_expression(const char* expression);
void urlhack_set_regular_expression(int mode, const char* expression) ;

void urlhack_init();
void urlhack_cleanup();

void SetHyperlinkFlag( const int flag ) ;

#endif // _URLHACK_H
