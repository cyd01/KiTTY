/*
 * Header file for the Objective-C parts of Mac OS X PuTTY. This
 * file contains the class definitions, which would cause compile
 * failures in the pure C modules if they appeared in osx.h.
 */

#ifndef PUTTY_OSXCLASS_H
#define PUTTY_OSXCLASS_H

#include "putty.h"

/*
 * The application controller class, defined in osxmain.m.
 */
@interface AppController : NSObject
{
    NSTimer *timer;
}
- (void)newSessionConfig:(id)sender;
- (void)newTerminal:(id)sender;
- (void)newSessionWithConfig:(id)cfg;
- (void)setTimer:(long)next;
@end
extern AppController *controller;

/*
 * The SessionWindow class, defined in osxwin.m.
 */

struct alert_queue {
    struct alert_queue *next;
    NSAlert *alert;
    void (*callback)(void *, int);
    void *ctx;
};

@class SessionWindow;
@class TerminalView;

@interface SessionWindow : NSWindow
{
    Terminal *term;
    TerminalView *termview;
    struct unicode_data ucsdata;
    void *logctx;
    Config cfg;
    void *ldisc;
    Backend *back;
    void *backhandle;
    int exited;
    /*
     * The following two members relate to the currently active
     * alert sheet, if any. They are NULL if there isn't one.
     */
    void (*alert_callback)(void *, int);
    void *alert_ctx;
    /* This queues future alerts that need to be shown. */
    struct alert_queue *alert_qhead, *alert_qtail;
}
- (id)initWithConfig:(Config)cfg;
- (void)drawStartFinish:(BOOL)start;
- (void)setColour:(int)n r:(float)r g:(float)g b:(float)b;
- (Config *)cfg;
- (void)doText:(wchar_t *)text len:(int)len x:(int)x y:(int)y
    attr:(unsigned long)attr lattr:(int)lattr;
- (int)fromBackend:(const char *)data len:(int)len isStderr:(int)is_stderr;
- (int)fromBackendUntrusted:(const char *)data len:(int)len;
- (void)startAlert:(NSAlert *)alert
    withCallback:(void (*)(void *, int))callback andCtx:(void *)ctx;
- (void)endSession:(int)clean;
- (void)notifyRemoteExit;
- (Terminal *)term;
@end

/*
 * The ConfigWindow class, defined in osxdlg.m.
 */

@class ConfigWindow;

@interface ConfigWindow : NSWindow
{
    NSOutlineView *treeview;
    struct controlbox *ctrlbox;
    void *dv;
    Config cfg;
}
- (id)initWithConfig:(Config)cfg;
@end

/*
 * Functions exported by osxctrls.m. (They have to go in this
 * header file and not osx.h, because some of them have Cocoa class
 * types in their prototypes.)
 */
#define HSPACING 12		       /* needed in osxdlg.m and osxctrls.m */
#define VSPACING 8

void *fe_dlg_init(void *data, NSWindow *window, NSObject *target, SEL action);
void fe_dlg_free(void *dv);
void create_ctrls(void *dv, NSView *parent, struct controlset *s,
		  int *minw, int *minh);
int place_ctrls(void *dv, struct controlset *s, int leftx, int topy,
		int width);	       /* returns height used */
void select_panel(void *dv, struct controlbox *b, const char *name);

#endif /* PUTTY_OSXCLASS_H */
