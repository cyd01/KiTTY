/*
 * osxdlg.m: various PuTTY dialog boxes for OS X.
 */

#import <Cocoa/Cocoa.h>
#include "putty.h"
#include "storage.h"
#include "dialog.h"
#include "osxclass.h"

/*
 * The `ConfigWindow' class is used to start up a new PuTTY
 * session.
 */

@class ConfigTree;
@interface ConfigTree : NSObject
{
    NSString **paths;
    int *levels;
    int nitems, itemsize;
}
- (void)addPath:(char *)path;
@end

@implementation ConfigTree
- (id)init
{
    self = [super init];
    paths = NULL;
    levels = NULL;
    nitems = itemsize = 0;
    return self;
}
- (void)addPath:(char *)path
{
    if (nitems >= itemsize) {
	itemsize += 32;
	paths = sresize(paths, itemsize, NSString *);
	levels = sresize(levels, itemsize, int);
    }
    paths[nitems] = [[NSString stringWithCString:path] retain];
    levels[nitems] = ctrl_path_elements(path) - 1;
    nitems++;
}
- (void)dealloc
{
    int i;

    for (i = 0; i < nitems; i++)
	[paths[i] release];

    sfree(paths);
    sfree(levels);

    [super dealloc];
}
- (id)iterateChildren:(int)index ofItem:(id)item count:(int *)count
{
    int i, plevel;

    if (item) {
	for (i = 0; i < nitems; i++)
	    if (paths[i] == item)
		break;
	assert(i < nitems);
	plevel = levels[i];
	i++;
    } else {
	i = 0;
	plevel = -1;
    }

    if (count)
	*count = 0;

    while (index > 0) {
	if (i >= nitems || levels[i] != plevel+1)
	    return nil;
	if (count)
	    (*count)++;
	do {
	    i++;
	} while (i < nitems && levels[i] > plevel+1);
	index--;
    }

    return paths[i];
}
- (id)outlineView:(NSOutlineView *)outlineView child:(int)index ofItem:(id)item
{
    return [self iterateChildren:index ofItem:item count:NULL];
}
- (int)outlineView:(NSOutlineView *)outlineView numberOfChildrenOfItem:(id)item
{
    int count = 0;
    /* pass nitems+1 to ensure we run off the end */
    [self iterateChildren:nitems+1 ofItem:item count:&count];
    return count;
}
- (BOOL)outlineView:(NSOutlineView *)outlineView isItemExpandable:(id)item
{
    return [self outlineView:outlineView numberOfChildrenOfItem:item] > 0;
}
- (id)outlineView:(NSOutlineView *)outlineView objectValueForTableColumn:(NSTableColumn *)tableColumn byItem:(id)item
{
    /*
     * Trim off all path elements except the last one.
     */
    NSArray *components = [item componentsSeparatedByString:@"/"];
    return [components objectAtIndex:[components count]-1];
}
@end

@implementation ConfigWindow
- (id)initWithConfig:(Config)aCfg
{
    NSScrollView *scrollview;
    NSTableColumn *col;
    ConfigTree *treedata;
    int by = 0, mby = 0;
    int wmin = 0;
    int hmin = 0;
    int panelht = 0;

    ctrlbox = ctrl_new_box();
    setup_config_box(ctrlbox, FALSE /*midsession*/, aCfg.protocol,
		     0 /* protcfginfo */);
    unix_setup_config_box(ctrlbox, FALSE /*midsession*/, aCfg.protocol);

    cfg = aCfg;			       /* structure copy */

    self = [super initWithContentRect:NSMakeRect(0,0,300,300)
	    styleMask:(NSTitledWindowMask | NSMiniaturizableWindowMask |
		       NSClosableWindowMask)
	    backing:NSBackingStoreBuffered
	    defer:YES];
    [self setTitle:@"PuTTY Configuration"];

    [self setIgnoresMouseEvents:NO];

    dv = fe_dlg_init(&cfg, self, self, @selector(configBoxFinished:));

    scrollview = [[NSScrollView alloc] initWithFrame:NSMakeRect(20,20,10,10)];
    treeview = [[NSOutlineView alloc] initWithFrame:[scrollview frame]];
    [scrollview setBorderType:NSLineBorder];
    [scrollview setDocumentView:treeview];
    [[self contentView] addSubview:scrollview];
    [scrollview setHasVerticalScroller:YES];
    [scrollview setAutohidesScrollers:YES];
    /* FIXME: the below is untested. Test it then remove this notice. */
    [treeview setAllowsColumnReordering:NO];
    [treeview setAllowsColumnResizing:NO];
    [treeview setAllowsMultipleSelection:NO];
    [treeview setAllowsEmptySelection:NO];
    [treeview setAllowsColumnSelection:YES];

    treedata = [[[ConfigTree alloc] init] retain];

    col = [[NSTableColumn alloc] initWithIdentifier:nil];
    [treeview addTableColumn:col];
    [treeview setOutlineTableColumn:col];

    [[treeview headerView] setFrame:NSMakeRect(0,0,0,0)];

    /*
     * Create the controls.
     */
    {
	int i;
	char *path = NULL;

	for (i = 0; i < ctrlbox->nctrlsets; i++) {
	    struct controlset *s = ctrlbox->ctrlsets[i];
	    int mw, mh;

	    if (!*s->pathname) {

		create_ctrls(dv, [self contentView], s, &mw, &mh);

		by += 20 + mh;

		if (wmin < mw + 40)
		    wmin = mw + 40;
	    } else {
		int j = path ? ctrl_path_compare(s->pathname, path) : 0;

		if (j != INT_MAX) {    /* add to treeview, start new panel */
		    char *c;

		    /*
		     * We expect never to find an implicit path
		     * component. For example, we expect never to
		     * see A/B/C followed by A/D/E, because that
		     * would _implicitly_ create A/D. All our path
		     * prefixes are expected to contain actual
		     * controls and be selectable in the treeview;
		     * so we would expect to see A/D _explicitly_
		     * before encountering A/D/E.
		     */
		    assert(j == ctrl_path_elements(s->pathname) - 1);

		    c = strrchr(s->pathname, '/');
		    if (!c)
			c = s->pathname;
		    else
			c++;

		    [treedata addPath:s->pathname];
		    path = s->pathname;

		    panelht = 0;
		}

		create_ctrls(dv, [self contentView], s, &mw, &mh);
		if (wmin < mw + 3*20+150)
		    wmin = mw + 3*20+150;
		panelht += mh + 20;
		if (hmin < panelht - 20)
		    hmin = panelht - 20;
	    }
	}
    }

    {
	int i;
	NSRect r;

	[treeview setDataSource:treedata];
	for (i = [treeview numberOfRows]; i-- ;)
	    [treeview expandItem:[treeview itemAtRow:i] expandChildren:YES];

	[treeview sizeToFit];
	r = [treeview frame];
	if (hmin < r.size.height)
	    hmin = r.size.height;
    }

    [self setContentSize:NSMakeSize(wmin, hmin+60+by)];
    [scrollview setFrame:NSMakeRect(20, 40+by, 150, hmin)];
    [treeview setDelegate:self];
    mby = by;

    /*
     * Now place the controls.
     */
    {
	int i;
	char *path = NULL;
	panelht = 0;

	for (i = 0; i < ctrlbox->nctrlsets; i++) {
	    struct controlset *s = ctrlbox->ctrlsets[i];

	    if (!*s->pathname) {
		by -= VSPACING + place_ctrls(dv, s, 20, by, wmin-40);
	    } else {
		if (!path || strcmp(s->pathname, path))
		    panelht = 0;

		panelht += VSPACING + place_ctrls(dv, s, 2*20+150,
						  40+mby+hmin-panelht,
						  wmin - (3*20+150));

		path = s->pathname;
	    }
	}
    }

    select_panel(dv, ctrlbox, [[treeview itemAtRow:0] cString]);

    [treeview reloadData];

    dlg_refresh(NULL, dv);

    [self center];		       /* :-) */

    return self;
}
- (void)configBoxFinished:(id)object
{
    int ret = [object intValue];       /* it'll be an NSNumber */
    if (ret) {
	[controller performSelectorOnMainThread:
	 @selector(newSessionWithConfig:)
	 withObject:[NSData dataWithBytes:&cfg length:sizeof(cfg)]
	 waitUntilDone:NO];
    }
    [self close];
}
- (void)outlineViewSelectionDidChange:(NSNotification *)notification
{
    const char *path = [[treeview itemAtRow:[treeview selectedRow]] cString];
    select_panel(dv, ctrlbox, path);
}
- (BOOL)outlineView:(NSOutlineView *)outlineView
    shouldEditTableColumn:(NSTableColumn *)tableColumn item:(id)item
{
    return NO;			       /* no editing! */
}
@end

/* ----------------------------------------------------------------------
 * Various special-purpose dialog boxes.
 */

struct appendstate {
    void (*callback)(void *ctx, int result);
    void *ctx;
};

static void askappend_callback(void *ctx, int result)
{
    struct appendstate *state = (struct appendstate *)ctx;

    state->callback(state->ctx, (result == NSAlertFirstButtonReturn ? 2 :
				 result == NSAlertSecondButtonReturn ? 1 : 0));
    sfree(state);
}

int askappend(void *frontend, Filename filename,
	      void (*callback)(void *ctx, int result), void *ctx)
{
    static const char msgtemplate[] =
	"The session log file \"%s\" already exists. "
	"You can overwrite it with a new session log, "
	"append your session log to the end of it, "
	"or disable session logging for this session.";

    char *text;
    SessionWindow *win = (SessionWindow *)frontend;
    struct appendstate *state;
    NSAlert *alert;

    text = dupprintf(msgtemplate, filename.path);

    state = snew(struct appendstate);
    state->callback = callback;
    state->ctx = ctx;

    alert = [[NSAlert alloc] init];
    [alert setInformativeText:[NSString stringWithCString:text]];
    [alert addButtonWithTitle:@"Overwrite"];
    [alert addButtonWithTitle:@"Append"];
    [alert addButtonWithTitle:@"Disable"];
    [win startAlert:alert withCallback:askappend_callback andCtx:state];

    return -1;
}

struct algstate {
    void (*callback)(void *ctx, int result);
    void *ctx;
};

static void askalg_callback(void *ctx, int result)
{
    struct algstate *state = (struct algstate *)ctx;

    state->callback(state->ctx, result == NSAlertFirstButtonReturn);
    sfree(state);
}

int askalg(void *frontend, const char *algtype, const char *algname,
	   void (*callback)(void *ctx, int result), void *ctx)
{
    static const char msg[] =
	"The first %s supported by the server is "
	"%s, which is below the configured warning threshold.\n"
	"Continue with connection?";

    char *text;
    SessionWindow *win = (SessionWindow *)frontend;
    struct algstate *state;
    NSAlert *alert;

    text = dupprintf(msg, algtype, algname);

    state = snew(struct algstate);
    state->callback = callback;
    state->ctx = ctx;

    alert = [[NSAlert alloc] init];
    [alert setInformativeText:[NSString stringWithCString:text]];
    [alert addButtonWithTitle:@"Yes"];
    [alert addButtonWithTitle:@"No"];
    [win startAlert:alert withCallback:askalg_callback andCtx:state];

    return -1;
}

struct hostkeystate {
    char *host, *keytype, *keystr;
    int port;
    void (*callback)(void *ctx, int result);
    void *ctx;
};

static void verify_ssh_host_key_callback(void *ctx, int result)
{
    struct hostkeystate *state = (struct hostkeystate *)ctx;

    if (result == NSAlertThirdButtonReturn)   /* `Accept' */
	store_host_key(state->host, state->port,
		       state->keytype, state->keystr);
    state->callback(state->ctx, result != NSAlertFirstButtonReturn);
    sfree(state->host);
    sfree(state->keytype);
    sfree(state->keystr);
    sfree(state);
}

int verify_ssh_host_key(void *frontend, char *host, int port, char *keytype,
                        char *keystr, char *fingerprint,
                        void (*callback)(void *ctx, int result), void *ctx)
{
    static const char absenttxt[] =
	"The server's host key is not cached. You have no guarantee "
	"that the server is the computer you think it is.\n"
	"The server's %s key fingerprint is:\n"
	"%s\n"
	"If you trust this host, press \"Accept\" to add the key to "
	"PuTTY's cache and carry on connecting.\n"
	"If you want to carry on connecting just once, without "
	"adding the key to the cache, press \"Connect Once\".\n"
	"If you do not trust this host, press \"Cancel\" to abandon the "
	"connection.";
    static const char wrongtxt[] =
	"WARNING - POTENTIAL SECURITY BREACH!\n"
	"The server's host key does not match the one PuTTY has "
	"cached. This means that either the server administrator "
	"has changed the host key, or you have actually connected "
	"to another computer pretending to be the server.\n"
	"The new %s key fingerprint is:\n"
	"%s\n"
	"If you were expecting this change and trust the new key, "
	"press \"Accept\" to update PuTTY's cache and continue connecting.\n"
	"If you want to carry on connecting but without updating "
	"the cache, press \"Connect Once\".\n"
	"If you want to abandon the connection completely, press "
	"\"Cancel\" to cancel. Pressing \"Cancel\" is the ONLY guaranteed "
	"safe choice.";

    int ret;
    char *text;
    SessionWindow *win = (SessionWindow *)frontend;
    struct hostkeystate *state;
    NSAlert *alert;

    /*
     * Verify the key.
     */
    ret = verify_host_key(host, port, keytype, keystr);

    if (ret == 0)
	return 1;

    text = dupprintf((ret == 2 ? wrongtxt : absenttxt), keytype, fingerprint);

    state = snew(struct hostkeystate);
    state->callback = callback;
    state->ctx = ctx;
    state->host = dupstr(host);
    state->port = port;
    state->keytype = dupstr(keytype);
    state->keystr = dupstr(keystr);

    alert = [[NSAlert alloc] init];
    [alert setInformativeText:[NSString stringWithCString:text]];
    [alert addButtonWithTitle:@"Cancel"];
    [alert addButtonWithTitle:@"Connect Once"];
    [alert addButtonWithTitle:@"Accept"];
    [win startAlert:alert withCallback:verify_ssh_host_key_callback
     andCtx:state];

    return -1;
}

void old_keyfile_warning(void)
{
    /*
     * This should never happen on OS X. We hope.
     */
}

static void connection_fatal_callback(void *ctx, int result)
{
    SessionWindow *win = (SessionWindow *)ctx;

    [win endSession:FALSE];
}

void connection_fatal(void *frontend, char *p, ...)
{
    SessionWindow *win = (SessionWindow *)frontend;
    va_list ap;
    char *msg;
    NSAlert *alert;

    va_start(ap, p);
    msg = dupvprintf(p, ap);
    va_end(ap);

    alert = [[NSAlert alloc] init];
    [alert setInformativeText:[NSString stringWithCString:msg]];
    [alert addButtonWithTitle:@"Proceed"];
    [win startAlert:alert withCallback:connection_fatal_callback
     andCtx:win];
}
