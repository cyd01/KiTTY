/*
 * HACK: PuttyTray / Nutty
 * Hyperlink stuff: CORE FILE! Don't forget to COPY IT TO THE NEXT VERSION
 */
#include <windows.h>
#include <string.h>
#include "urlhack.h"
#include "misc.h"
#include "puttymem.h"
#include <assert.h>

extern int debug_flag ;
void debug_logevent( const char *fmt, ... ) ;

int urlhack_mouse_old_x = -1, urlhack_mouse_old_y = -1, urlhack_current_region = -1;

static text_region **link_regions;
static unsigned int link_regions_len;
static unsigned int link_regions_current_pos;

// Essai de regex qui accepte aussi les lien mailto://
const char* urlhack_default_regex = " (((((https?|ftp|svn):\\/\\/)|www\\.)(([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)|localhost|([a-zA-Z0-9\\-]+\\.)*[a-zA-Z0-9\\-]+\\.(com|net|org|info|biz|int|gov|name|edu|[a-zA-Z][a-zA-Z]))(:[0-9]+)?((\\/|\\?)[^ \"]*[^ ,;\\.:\">)])?)|(mailto:\\/\\/[a-zA-Z0-9\\-_\\.]+@[a-zA-Z0-9\\-_\\.]+\\.[a-z]{2,}))" ;


// Celle-là marche c'est sûr
//const char* urlhack_default_regex =  "(((https?|ftp):\\/\\/)|www\\.)(([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)|localhost|([a-zA-Z0-9\\-]+\\.)*[a-zA-Z0-9\\-]+\\.(com|net|org|info|biz|int|gov|name|edu|[a-zA-Z][a-zA-Z]))(:[0-9]+)?((\\/|\\?)[^ \"]*[^ ,;\\.:\">)])?";

const char* urlhack_liberal_regex =
    "("
        "([a-zA-Z]+://|[wW][wW][wW]\\.|spotify:|telnet:)"
        "[^ '\")>]+"
    ")"
    ;

int urlhack_is_in_link_region(int x, int y)
{
    unsigned int i = 0;

    while (i != link_regions_current_pos) {
        text_region r = *link_regions[i];

        if ((r.y0 == r.y1 && y == r.y0 && y == r.y1 && x >= r.x0 && x < r.x1) ||
            (r.y0 != r.y1 && ((y == r.y0 && x >= r.x0) || (y == r.y1 && x < r.x1) || (y > r.y0 && y < r.y1))))
            return i+1;
        i++;
    }
    
    return 0;
}

int urlhack_is_in_this_link_region(text_region r, int x, int y)
{
    if ((r.y0 == r.y1 && y == r.y0 && y == r.y1 && x >= r.x0 && x < r.x1) || 
        (r.y0 != r.y1 && ((y == r.y0 && x >= r.x0) || (y == r.y1 && x < r.x1) || (y > r.y0 && y < r.y1)))) {
        return 1;
    }
    
    return 0;
}

text_region urlhack_get_link_bounds(int x, int y)
{
    unsigned int i = 0;
    text_region region;

    while (i != link_regions_current_pos) {
        text_region r = *link_regions[i];

        if ((r.y0 == r.y1 && y == r.y0 && y == r.y1 && x >= r.x0 && x < r.x1) ||
            (r.y0 != r.y1 && ((y == r.y0 && x >= r.x0) || (y == r.y1 && x < r.x1) || (y > r.y0 && y < r.y1)))) {
            return *link_regions[i];
        }

        i++;
    }

    region.x0 = region.y0 = region.x1 = region.y1 = -1;
    return region;
}

text_region urlhack_get_link_region(int index)
{
    text_region region;

    if (index < 0 || index >= link_regions_current_pos) {
        region.x0 = region.y0 = region.x1 = region.y1 = -1;
        return region;
    }
    else {
        return *link_regions[index];
    }
}

void urlhack_add_link_region(int x0, int y0, int x1, int y1)
{
    if (link_regions_current_pos >= link_regions_len) {
        unsigned int i;
        link_regions_len *= 2;
        link_regions = sresize(link_regions, link_regions_len, text_region*);
        for (i = link_regions_current_pos; i < link_regions_len; ++i) {
            link_regions[i] = NULL;
        }
    }

    link_regions[link_regions_current_pos] = snew(text_region);
    link_regions[link_regions_current_pos]->x0 = x0;
    link_regions[link_regions_current_pos]->y0 = y0;
    link_regions[link_regions_current_pos]->x1 = x1;
    link_regions[link_regions_current_pos]->y1 = y1;

    link_regions_current_pos++;
}

void urlhack_launch_url(const char* app, const char *url)
{
    if (app) {
	if( debug_flag ) { debug_logevent("Hyperlink: %s %s", app, url); }
        ShellExecute(NULL, NULL, app, url, NULL, SW_SHOWNORMAL);
    } else {
	if( debug_flag ) { debug_logevent("Hyperlink: \"open\" %s", url); }
        ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
    }
}

int urlhack_is_ctrl_pressed()
{
    return HIWORD(GetAsyncKeyState(VK_CONTROL));
}

void urlhack_link_regions_clear()
{
    unsigned int i;
    for (i = 0; i < link_regions_len; ++i) {
        if (link_regions[i] != NULL) {
            sfree(link_regions[i]);
            link_regions[i] = NULL;
        }
    }
    link_regions_current_pos = 0;
}

// Regular expression stuff
static int urlhack_disabled = 0;
static int is_regexp_compiled = 0;
static regex_t urlhack_rx ;
static char *window_text;
static int window_text_len;
static int window_text_current_pos;
void urlhack_enable(void){
	urlhack_disabled=0;
}
void urlhack_init()
{
    unsigned int i;

    /* 32 links seems like a sane base value */
    link_regions_current_pos = 0;
    link_regions_len = 32;
    link_regions = snewn(link_regions_len, text_region*);

    for (i = 0; i < link_regions_len; ++i) {
        link_regions[i] = NULL;
    }

    /* Start with default terminal size */
    //window_text_len = 80*24+1;
    window_text_len = 500*300+1;
    window_text = snewn(window_text_len, char);
    urlhack_reset();
}

void urlhack_cleanup()
{
    urlhack_link_regions_clear();
    sfree(link_regions);
    sfree(window_text);
}

void urlhack_putchar(char ch)
{
    if (window_text_current_pos >= window_text_len) {
        window_text = sresize(window_text, 2 * window_text_len, char);
        memset(window_text + window_text_current_pos, '\0', window_text_len - window_text_current_pos);
        window_text_len *= 2;
    }
    window_text[window_text_current_pos++] = ch;
}

void urlhack_reset()
{
    memset(window_text, '\0', window_text_len);
    window_text_current_pos = 0;
}

static void rtfm(char *error)
{
    char std_msg[] = "The following error occured when compiling the regular expression\n" \
        "for the hyperlink support. Hyperlink detection is disabled during\n" \
        "this session (restart to try again).\n\n";

    char *full_msg = dupprintf("%s%s", std_msg, error);

	urlhack_disabled = 1 ;
	//SetHyperlinkFlag(0);
	
    MessageBox(0, full_msg, "Hyperlink patch error", MB_OK);
    free(full_msg);
	
}

void logevent(void *frontend, const char *string);

static void (*regerror_func)( char* s) = 0;
void set_regerror_func( void (*func)( char*))
{
	regerror_func = func;
}

#ifndef REG_NOSUB
#define REG_NOSUB 0004
#endif
void urlhack_set_regular_expression(int mode, const char* expression)
{
#ifndef NO_HYPERLINK
    char *to_use=NULL;
    switch (mode) {
    case URLHACK_REGEX_CUSTOM:
	if( to_use!= NULL) { free(to_use) ; }
	to_use = (char*)malloc(strlen(expression)+1); 
	strcpy(to_use,expression);
        //to_use = expression;
        break;
    case URLHACK_REGEX_CLASSIC:
	if( to_use!= NULL) { free(to_use) ; }
	to_use = (char*)malloc(strlen(urlhack_default_regex)+1); 
	strcpy(to_use,urlhack_default_regex);
        //to_use = urlhack_default_regex;
        break;
    case URLHACK_REGEX_LIBERAL:
	if( to_use!= NULL) { free(to_use) ; }
	to_use = (char*)malloc(strlen(urlhack_liberal_regex)+1); 
	strcpy(to_use,urlhack_liberal_regex);
        //to_use = urlhack_liberal_regex;
        break;
    default:
        assert(!"illegal default regex setting");
    }
   
    if( is_regexp_compiled ) { 
	regfree(&urlhack_rx);
	is_regexp_compiled = 0;
    }
        //set_regerror_func(rtfm);
	int result ;
	if( (result=regcomp(&urlhack_rx,(char*)(to_use),REG_EXTENDED)) != 0 ){
		urlhack_disabled = 1;
		char buffer[512]="";
		regerror(result, &urlhack_rx, buffer, sizeof buffer);
		rtfm(buffer);
	} else { 
		is_regexp_compiled = 1 ; 
		logevent(NULL, "Hyperlink patch: regex successfully compiled" ) ;
	}
#endif
}

void urlhack_go_find_me_some_hyperlinks(int screen_width)
{
#ifndef NO_HYPERLINK
    char* text_pos;
	
    if( urlhack_disabled!=0 ) {
	    return ;
    }
    if (is_regexp_compiled == 0) {
        urlhack_set_regular_expression(URLHACK_REGEX_CLASSIC,urlhack_default_regex);
	if( !is_regexp_compiled ) return ;
    }
    urlhack_link_regions_clear();
    text_pos = window_text;
	regmatch_t groupArray;
	int error ;
    error = regexec(&urlhack_rx, text_pos, 1, &groupArray ,0) ;
    while( error==0 ) {

	    char* start_pos = text_pos + groupArray.rm_so ; if(start_pos[0]==' ') start_pos++ ;

        int x0 = (start_pos - window_text) % screen_width;
        int y0 = (start_pos - window_text) / screen_width;
	    
	int x1 = (text_pos + groupArray.rm_eo - window_text) % screen_width;
        int y1 = (text_pos + groupArray.rm_eo - window_text) / screen_width;
	    
	if (x0 >= screen_width) x0 = screen_width - 1;
        if (x1 >= screen_width) x1 = screen_width - 1;
        urlhack_add_link_region(x0, y0, x1, y1);
		    
	text_pos = text_pos + groupArray.rm_eo + 1;
	error = regexec(&urlhack_rx, text_pos, 1, &groupArray ,REG_NOTBOL) ;
	}
#endif
}


/*
Function pour corriger le probleme de mauvaise regex !
*/
void InitRegistryAllSessions( HKEY hMainKey, LPCTSTR lpSubKey, char * SubKeyName, char * filename, char * text ) ;
void FixWrongRegex() {
	char *st;
	st = (char*) malloc(strlen(urlhack_default_regex)+100);
	sprintf(st,"\"HyperlinkRegularExpression\"=\"%s\"",urlhack_default_regex);
	InitRegistryAllSessions( HKEY_CURRENT_USER, "Software\\9bis.com\\KiTTY", "Sessions", "hyperlinkfix.reg",st ) ;
	free(st);
}
