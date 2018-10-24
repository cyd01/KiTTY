/*
 * osxmain.m: main-program file of Mac OS X PuTTY.
 */

#import <Cocoa/Cocoa.h>

#define PUTTY_DO_GLOBALS	       /* actually _define_ globals */

#include "putty.h"
#include "osxclass.h"

/* ----------------------------------------------------------------------
 * Global variables.
 */

AppController *controller;

/* ----------------------------------------------------------------------
 * Miscellaneous elements of the interface to the cross-platform
 * and Unix PuTTY code.
 */

char *platform_get_x_display(void) {
    return NULL;
}

FontSpec platform_default_fontspec(const char *name)
{
    FontSpec ret;
    /* FIXME */
    return ret;
}

Filename platform_default_filename(const char *name)
{
    Filename ret;
    if (!strcmp(name, "LogFileName"))
	strcpy(ret.path, "putty.log");
    else
	*ret.path = '\0';
    return ret;
}

char *platform_default_s(const char *name)
{
    return NULL;
}

int platform_default_i(const char *name, int def)
{
    if (!strcmp(name, "CloseOnExit"))
	return 2;  /* maps to FORCE_ON after painful rearrangement :-( */
    return def;
}

char *x_get_default(const char *key)
{
    return NULL;		       /* this is a stub */
}

static void commonfatalbox(char *p, va_list ap)
{
    char errorbuf[2048];
    NSAlert *alert;

    /*
     * We may have come here because we ran out of memory, in which
     * case it's entirely likely that that further memory
     * allocations will fail. So (a) we use vsnprintf to format the
     * error message rather than the usual dupvprintf; and (b) we
     * have a fallback way to get the message out via stderr if
     * even creating an NSAlert fails.
     */
    vsnprintf(errorbuf, lenof(errorbuf), p, ap);

    alert = [NSAlert alloc];
    if (!alert) {
	fprintf(stderr, "fatal error (and NSAlert failed): %s\n", errorbuf);
    } else {
	alert = [[alert init] autorelease];
	[alert addButtonWithTitle:@"Terminate"];
	[alert setInformativeText:[NSString stringWithCString:errorbuf]];
	[alert runModal];
    }
    exit(1);
}

void nonfatal(void *frontend, char *p, ...)
{
    char *errorbuf;
    NSAlert *alert;
    va_list ap;

    va_start(ap, p);
    errorbuf = dupvprintf(p, ap);
    va_end(ap);

    alert = [[[NSAlert alloc] init] autorelease];
    [alert addButtonWithTitle:@"Error"];
    [alert setInformativeText:[NSString stringWithCString:errorbuf]];
    [alert runModal];

    sfree(errorbuf);
}

void fatalbox(char *p, ...)
{
    va_list ap;
    va_start(ap, p);
    commonfatalbox(p, ap);
    va_end(ap);
}

void modalfatalbox(char *p, ...)
{
    va_list ap;
    va_start(ap, p);
    commonfatalbox(p, ap);
    va_end(ap);
}

void cmdline_error(char *p, ...)
{
    va_list ap;
    fprintf(stderr, "%s: ", appname);
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

/*
 * Clean up and exit.
 */
void cleanup_exit(int code)
{
    /*
     * Clean up.
     */
    sk_cleanup();
    random_save_seed();
    exit(code);
}

/* ----------------------------------------------------------------------
 * Tiny extension to NSMenuItem which carries a payload of a `void
 * *', allowing several menu items to invoke the same message but
 * pass different data through it.
 */
@interface DataMenuItem : NSMenuItem
{
    void *payload;
}
- (void)setPayload:(void *)d;
- (void *)getPayload;
@end
@implementation DataMenuItem
- (void)setPayload:(void *)d
{
    payload = d;
}
- (void *)getPayload
{
    return payload;
}
@end

/* ----------------------------------------------------------------------
 * Utility routines for constructing OS X menus.
 */

NSMenu *newmenu(const char *title)
{
    return [[[NSMenu allocWithZone:[NSMenu menuZone]]
	     initWithTitle:[NSString stringWithCString:title]]
	    autorelease];
}

NSMenu *newsubmenu(NSMenu *parent, const char *title)
{
    NSMenuItem *item;
    NSMenu *child;

    item = [[[NSMenuItem allocWithZone:[NSMenu menuZone]]
	     initWithTitle:[NSString stringWithCString:title]
	     action:NULL
	     keyEquivalent:@""]
	    autorelease];
    child = newmenu(title);
    [item setEnabled:YES];
    [item setSubmenu:child];
    [parent addItem:item];
    return child;
}

id initnewitem(NSMenuItem *item, NSMenu *parent, const char *title,
	       const char *key, id target, SEL action)
{
    unsigned mask = NSCommandKeyMask;

    if (key[strcspn(key, "-")]) {
	while (*key && *key != '-') {
	    int c = tolower((unsigned char)*key);
	    if (c == 's') {
		mask |= NSShiftKeyMask;
	    } else if (c == 'o' || c == 'a') {
		mask |= NSAlternateKeyMask;
	    }
	    key++;
	}
	if (*key)
	    key++;
    }

    item = [[item initWithTitle:[NSString stringWithCString:title]
	     action:NULL
	     keyEquivalent:[NSString stringWithCString:key]]
	    autorelease];

    if (*key)
	[item setKeyEquivalentModifierMask: mask];

    [item setEnabled:YES];
    [item setTarget:target];
    [item setAction:action];

    [parent addItem:item];

    return item;
}

NSMenuItem *newitem(NSMenu *parent, char *title, char *key,
		    id target, SEL action)
{
    return initnewitem([NSMenuItem allocWithZone:[NSMenu menuZone]],
		       parent, title, key, target, action);
}

/* ----------------------------------------------------------------------
 * AppController: the object which receives the messages from all
 * menu selections that aren't standard OS X functions.
 */
@implementation AppController

- (id)init
{
    self = [super init];
    timer = NULL;
    return self;
}

- (void)newTerminal:(id)sender
{
    id win;
    Config cfg;

    do_defaults(NULL, &cfg);

    cfg.protocol = -1;		       /* PROT_TERMINAL */

    win = [[SessionWindow alloc] initWithConfig:cfg];
    [win makeKeyAndOrderFront:self];
}

- (void)newSessionConfig:(id)sender
{
    id win;
    Config cfg;

    do_defaults(NULL, &cfg);

    win = [[ConfigWindow alloc] initWithConfig:cfg];
    [win makeKeyAndOrderFront:self];
}

- (void)newSessionWithConfig:(id)vdata
{
    id win;
    Config cfg;
    NSData *data = (NSData *)vdata;

    assert([data length] == sizeof(cfg));
    [data getBytes:&cfg];

    win = [[SessionWindow alloc] initWithConfig:cfg];
    [win makeKeyAndOrderFront:self];
}

- (NSMenu *)applicationDockMenu:(NSApplication *)sender
{
    NSMenu *menu = newmenu("Dock Menu");
    /*
     * FIXME: Add some useful things to this, probably including
     * the saved session list.
     */
    return menu;
}

- (void)timerFired:(id)sender
{
    long now, next;

    assert(sender == timer);

    /* `sender' is the timer itself, so its userInfo is an NSNumber. */
    now = [(NSNumber *)[sender userInfo] longValue];

    [sender invalidate];

    timer = NULL;

    if (run_timers(now, &next))
	[self setTimer:next];
}

- (void)setTimer:(long)next
{
    long interval = next - GETTICKCOUNT();
    float finterval;

    if (interval <= 0)
	interval = 1;		       /* just in case */

    finterval = interval / (float)TICKSPERSEC;

    if (timer) {
	[timer invalidate];
    }

    timer = [NSTimer scheduledTimerWithTimeInterval:finterval
	     target:self selector:@selector(timerFired:)
	     userInfo:[NSNumber numberWithLong:next] repeats:NO];
}

@end

void timer_change_notify(long next)
{
    [controller setTimer:next];
}

/* ----------------------------------------------------------------------
 * Annoyingly, it looks as if I have to actually subclass
 * NSApplication if I want to catch NSApplicationDefined events. So
 * here goes.
 */
@interface MyApplication : NSApplication
{
}
@end
@implementation MyApplication
- (void)sendEvent:(NSEvent *)ev
{
    if ([ev type] == NSApplicationDefined)
	osxsel_process_results();

    [super sendEvent:ev];
}    
@end

/* ----------------------------------------------------------------------
 * Main program. Constructs the menus and runs the application.
 */
int main(int argc, char **argv)
{
    NSAutoreleasePool *pool;
    NSMenu *menu;
    NSMenuItem *item;
    NSImage *icon;

    pool = [[NSAutoreleasePool alloc] init];

    icon = [NSImage imageNamed:@"NSApplicationIcon"];
    [MyApplication sharedApplication];
    [NSApp setApplicationIconImage:icon];

    controller = [[[AppController alloc] init] autorelease];
    [NSApp setDelegate:controller];

    [NSApp setMainMenu: newmenu("Main Menu")];

    menu = newsubmenu([NSApp mainMenu], "Apple Menu");
    [NSApp setServicesMenu:newsubmenu(menu, "Services")];
    [menu addItem:[NSMenuItem separatorItem]];
    item = newitem(menu, "Hide PuTTY", "h", NSApp, @selector(hide:));
    item = newitem(menu, "Hide Others", "o-h", NSApp, @selector(hideOtherApplications:));
    item = newitem(menu, "Show All", "", NSApp, @selector(unhideAllApplications:));
    [menu addItem:[NSMenuItem separatorItem]];
    item = newitem(menu, "Quit", "q", NSApp, @selector(terminate:));
    [NSApp setAppleMenu: menu];

    menu = newsubmenu([NSApp mainMenu], "File");
    item = newitem(menu, "New", "n", NULL, @selector(newSessionConfig:));
    item = newitem(menu, "New Terminal", "t", NULL, @selector(newTerminal:));
    item = newitem(menu, "Close", "w", NULL, @selector(performClose:));

    menu = newsubmenu([NSApp mainMenu], "Window");
    [NSApp setWindowsMenu: menu];
    item = newitem(menu, "Minimise Window", "m", NULL, @selector(performMiniaturize:));

//    menu = newsubmenu([NSApp mainMenu], "Help");
//    item = newitem(menu, "PuTTY Help", "?", NSApp, @selector(showHelp:));

    /*
     * Start up the sub-thread doing select().
     */
    osxsel_init();

    /*
     * Start up networking.
     */
    sk_init();

    /*
     * FIXME: To make initial debugging more convenient I'm going
     * to start by opening a session window unconditionally. This
     * will probably change later on.
     */
    [controller newSessionConfig:nil];

    [NSApp run];
    [pool release];

    return 0;
}
