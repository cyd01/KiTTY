/*
 * osxctrls.m: OS X implementation of the dialog.h interface.
 */

#import <Cocoa/Cocoa.h>
#include "putty.h"
#include "dialog.h"
#include "osxclass.h"
#include "tree234.h"

/*
 * Still to be implemented:
 * 
 *  - file selectors (NSOpenPanel / NSSavePanel)
 * 
 *  - font selectors
 *  - colour selectors
 *     * both of these have a conceptual oddity in Cocoa that
 * 	 you're only supposed to have one per application. But I
 * 	 currently expect to be able to have multiple PuTTY config
 * 	 boxes on screen at once; what happens if you trigger the
 * 	 font selector in each one at the same time?
 *     * if it comes to that, the _font_ selector can probably be
 * 	 managed by other means: nobody is forcing me to implement
 * 	 a font selector using a `Change...' button. The portable
 * 	 dialog interface gives me the flexibility to do this how I
 * 	 want.
 *     * The colour selector interface, in its present form, is
 * 	 more interesting and _if_ a radical change of plan is
 * 	 required then it may stretch across the interface into the
 * 	 portable side.
 *     * Before I do anything rash I should start by looking at the
 * 	 Mac Classic port and see how it's done there, on the basis
 * 	 that Apple seem reasonably unlikely to have invented this
 * 	 crazy restriction specifically for OS X.
 * 
 *  - focus management
 *     * I tried using makeFirstResponder to give keyboard focus,
 * 	 but it appeared not to work. Try again, and work out how
 * 	 it should be done.
 *     * also look into tab order. Currently pressing Tab suggests
 * 	 that only edit boxes and list boxes can get the keyboard
 * 	 focus, and that buttons (in all their forms) are unable to
 * 	 be driven by the keyboard. Find out for sure.
 * 
 *  - dlg_error_msg
 *     * this may run into the usual aggro with modal dialog boxes.
 */

/*
 * For Cocoa control layout, I need a two-stage process. In stage
 * one, I allocate all the controls and measure their natural
 * sizes, which allows me to compute the _minimum_ width and height
 * of a given section of dialog. Then, in stage two, I lay out the
 * dialog box as a whole, decide how much each section of the box
 * needs to receive, and assign it its final size.
 */

/*
 * As yet unsolved issues [FIXME]:
 * 
 *  - Sometimes the height returned from create_ctrls and the
 *    height returned from place_ctrls differ. Find out why. It may
 *    be harmless (e.g. results of NSTextView being odd), but I
 *    want to know.
 * 
 *  - NSTextViews are indented a bit. It'd be nice to put their
 *    left margin at the same place as everything else's.
 * 
 *  - I don't yet know whether we even _can_ support tab order or
 *    keyboard shortcuts. If we can't, then fair enough, we can't.
 *    But if we can, we should.
 * 
 *  - I would _really_ like to know of a better way to correct
 *    NSButton's stupid size estimates than by subclassing it and
 *    overriding sizeToFit with hard-wired sensible values!
 * 
 *  - Speaking of stupid size estimates, the amount by which I'm
 *    adjusting a titled NSBox (currently equal to the point size
 *    of its title font) looks as if it isn't _quite_ enough.
 *    Figure out what the real amount should be and use it.
 * 
 *  - I don't understand why there's always a scrollbar displayed
 *    in each list box. I thought I told it to autohide scrollers?
 * 
 *  - Why do I have to fudge list box heights by adding one? (Might
 *    it be to do with the missing header view?)
 */

/*
 * Subclass of NSButton which corrects the fact that the normal
 * one's sizeToFit method persistently returns 32 as its height,
 * which is simply a lie. I have yet to work out a better
 * alternative than hard-coding the real heights.
 */
@interface MyButton : NSButton
{
    int minht;
}
@end
@implementation MyButton
- (id)initWithFrame:(NSRect)r
{
    self = [super initWithFrame:r];
    minht = 25;
    return self;
}
- (void)setButtonType:(NSButtonType)t
{
    if (t == NSRadioButton || t == NSSwitchButton)
	minht = 18;
    else
	minht = 25;
    [super setButtonType:t];
}
- (void)sizeToFit
{
    NSRect r;
    [super sizeToFit];
    r = [self frame];
    r.size.height = minht;
    [self setFrame:r];
}
@end

/*
 * Class used as the data source for NSTableViews.
 */
@interface MyTableSource : NSObject
{
    tree234 *tree;
}
- (id)init;
- (void)add:(const char *)str withId:(int)id;
- (int)getid:(int)index;
- (void)swap:(int)index1 with:(int)index2;
- (void)removestr:(int)index;
- (void)clear;
@end
@implementation MyTableSource
- (id)init
{
    self = [super init];
    tree = newtree234(NULL);
    return self;
}
- (void)dealloc
{
    char *p;
    while ((p = delpos234(tree, 0)) != NULL)
	sfree(p);
    freetree234(tree);
    [super dealloc];
}
- (void)add:(const char *)str withId:(int)id
{
    addpos234(tree, dupprintf("%d\t%s", id, str), count234(tree));
}
- (int)getid:(int)index
{
    char *p = index234(tree, index);
    return atoi(p);
}
- (void)removestr:(int)index
{
    char *p = delpos234(tree, index);
    sfree(p);
}
- (void)swap:(int)index1 with:(int)index2
{
    char *p1, *p2;

    if (index1 > index2) {
	int t = index1; index1 = index2; index2 = t;
    }

    /* delete later one first so it doesn't affect index of earlier one */
    p2 = delpos234(tree, index2);
    p1 = delpos234(tree, index1);

    /* now insert earlier one before later one for the inverse reason */
    addpos234(tree, p2, index1);
    addpos234(tree, p1, index2);
}
- (void)clear
{
    char *p;
    while ((p = delpos234(tree, 0)) != NULL)
	sfree(p);
}
- (int)numberOfRowsInTableView:(NSTableView *)aTableView
{
    return count234(tree);
}
- (id)tableView:(NSTableView *)aTableView
    objectValueForTableColumn:(NSTableColumn *)aTableColumn
    row:(int)rowIndex
{
    int j = [[aTableColumn identifier] intValue];
    char *p = index234(tree, rowIndex);

    while (j >= 0) {
	p += strcspn(p, "\t");
	if (*p) p++;
	j--;
    }

    return [NSString stringWithCString:p length:strcspn(p, "\t")];
}
@end

/*
 * Object to receive messages from various control classes.
 */
@class Receiver;

struct fe_dlg {
    NSWindow *window;
    NSObject *target;
    SEL action;
    tree234 *byctrl;
    tree234 *bywidget;
    tree234 *boxes;
    void *data;			       /* passed to portable side */
    Receiver *rec;
};

@interface Receiver : NSObject
{
    struct fe_dlg *d;
}
- (id)initWithStruct:(struct fe_dlg *)aStruct;
@end

struct fe_ctrl {
    union control *ctrl;
    NSButton *button, *button2;
    NSTextField *label, *editbox;
    NSComboBox *combobox;
    NSButton **radiobuttons;
    NSTextView *textview;
    NSPopUpButton *popupbutton;
    NSTableView *tableview;
    NSScrollView *scrollview;
    int nradiobuttons;
};

static int fe_ctrl_cmp_by_ctrl(void *av, void *bv)
{
    struct fe_ctrl *a = (struct fe_ctrl *)av;
    struct fe_ctrl *b = (struct fe_ctrl *)bv;

    if (a->ctrl < b->ctrl)
	return -1;
    if (a->ctrl > b->ctrl)
	return +1;
    return 0;
}

static int fe_ctrl_find_by_ctrl(void *av, void *bv)
{
    union control *a = (union control *)av;
    struct fe_ctrl *b = (struct fe_ctrl *)bv;

    if (a < b->ctrl)
	return -1;
    if (a > b->ctrl)
	return +1;
    return 0;
}

struct fe_box {
    struct controlset *s;
    id box;
};

static int fe_boxcmp(void *av, void *bv)
{
    struct fe_box *a = (struct fe_box *)av;
    struct fe_box *b = (struct fe_box *)bv;

    if (a->s < b->s)
	return -1;
    if (a->s > b->s)
	return +1;
    return 0;
}

static int fe_boxfind(void *av, void *bv)
{
    struct controlset *a = (struct controlset *)av;
    struct fe_box *b = (struct fe_box *)bv;

    if (a < b->s)
	return -1;
    if (a > b->s)
	return +1;
    return 0;
}

struct fe_backwards {		       /* map Cocoa widgets back to fe_ctrls */
    id widget;
    struct fe_ctrl *c;
};

static int fe_backwards_cmp_by_widget(void *av, void *bv)
{
    struct fe_backwards *a = (struct fe_backwards *)av;
    struct fe_backwards *b = (struct fe_backwards *)bv;

    if (a->widget < b->widget)
	return -1;
    if (a->widget > b->widget)
	return +1;
    return 0;
}

static int fe_backwards_find_by_widget(void *av, void *bv)
{
    id a = (id)av;
    struct fe_backwards *b = (struct fe_backwards *)bv;

    if (a < b->widget)
	return -1;
    if (a > b->widget)
	return +1;
    return 0;
}

static struct fe_ctrl *fe_ctrl_new(union control *ctrl)
{
    struct fe_ctrl *c;

    c = snew(struct fe_ctrl);
    c->ctrl = ctrl;

    c->button = c->button2 = nil;
    c->label = nil;
    c->editbox = nil;
    c->combobox = nil;
    c->textview = nil;
    c->popupbutton = nil;
    c->tableview = nil;
    c->scrollview = nil;
    c->radiobuttons = NULL;
    c->nradiobuttons = 0;

    return c;
}

static void fe_ctrl_free(struct fe_ctrl *c)
{
    sfree(c->radiobuttons);
    sfree(c);
}

static struct fe_ctrl *fe_ctrl_byctrl(struct fe_dlg *d, union control *ctrl)
{
    return find234(d->byctrl, ctrl, fe_ctrl_find_by_ctrl);
}

static void add_box(struct fe_dlg *d, struct controlset *s, id box)
{
    struct fe_box *b = snew(struct fe_box);
    b->box = box;
    b->s = s;
    add234(d->boxes, b);
}

static id find_box(struct fe_dlg *d, struct controlset *s)
{
    struct fe_box *b = find234(d->boxes, s, fe_boxfind);
    return b ? b->box : NULL;
}

static void add_widget(struct fe_dlg *d, struct fe_ctrl *c, id widget)
{
    struct fe_backwards *b = snew(struct fe_backwards);
    b->widget = widget;
    b->c = c;
    add234(d->bywidget, b);
}

static struct fe_ctrl *find_widget(struct fe_dlg *d, id widget)
{
    struct fe_backwards *b = find234(d->bywidget, widget,
				     fe_backwards_find_by_widget);
    return b ? b->c : NULL;
}

void *fe_dlg_init(void *data, NSWindow *window, NSObject *target, SEL action)
{
    struct fe_dlg *d;

    d = snew(struct fe_dlg);
    d->window = window;
    d->target = target;
    d->action = action;
    d->byctrl = newtree234(fe_ctrl_cmp_by_ctrl);
    d->bywidget = newtree234(fe_backwards_cmp_by_widget);
    d->boxes = newtree234(fe_boxcmp);
    d->data = data;
    d->rec = [[Receiver alloc] initWithStruct:d];

    return d;
}

void fe_dlg_free(void *dv)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    struct fe_ctrl *c;
    struct fe_box *b;

    while ( (c = delpos234(d->byctrl, 0)) != NULL )
	fe_ctrl_free(c);
    freetree234(d->byctrl);

    while ( (c = delpos234(d->bywidget, 0)) != NULL )
	sfree(c);
    freetree234(d->bywidget);

    while ( (b = delpos234(d->boxes, 0)) != NULL )
	sfree(b);
    freetree234(d->boxes);

    [d->rec release];

    sfree(d);
}

@implementation Receiver
- (id)initWithStruct:(struct fe_dlg *)aStruct
{
    self = [super init];
    d = aStruct;
    return self;
}
- (void)buttonPushed:(id)sender
{
    struct fe_ctrl *c = find_widget(d, sender);

    assert(c && c->ctrl->generic.type == CTRL_BUTTON);
    c->ctrl->generic.handler(c->ctrl, d, d->data, EVENT_ACTION);
}
- (void)checkboxChanged:(id)sender
{
    struct fe_ctrl *c = find_widget(d, sender);

    assert(c && c->ctrl->generic.type == CTRL_CHECKBOX);
    c->ctrl->generic.handler(c->ctrl, d, d->data, EVENT_VALCHANGE);
}
- (void)radioChanged:(id)sender
{
    struct fe_ctrl *c = find_widget(d, sender);
    int j;

    assert(c && c->radiobuttons);
    for (j = 0; j < c->nradiobuttons; j++)
	if (sender != c->radiobuttons[j])
	    [c->radiobuttons[j] setState:NSOffState];
    c->ctrl->generic.handler(c->ctrl, d, d->data, EVENT_VALCHANGE);
}
- (void)popupMenuSelected:(id)sender
{
    struct fe_ctrl *c = find_widget(d, sender);
    c->ctrl->generic.handler(c->ctrl, d, d->data, EVENT_VALCHANGE);
}
- (void)controlTextDidChange:(NSNotification *)notification
{
    id widget = [notification object];
    struct fe_ctrl *c = find_widget(d, widget);
    assert(c && c->ctrl->generic.type == CTRL_EDITBOX);
    c->ctrl->generic.handler(c->ctrl, d, d->data, EVENT_VALCHANGE);
}
- (void)controlTextDidEndEditing:(NSNotification *)notification
{
    id widget = [notification object];
    struct fe_ctrl *c = find_widget(d, widget);
    assert(c && c->ctrl->generic.type == CTRL_EDITBOX);
    c->ctrl->generic.handler(c->ctrl, d, d->data, EVENT_REFRESH);
}
- (void)tableViewSelectionDidChange:(NSNotification *)notification
{
    id widget = [notification object];
    struct fe_ctrl *c = find_widget(d, widget);
    assert(c && c->ctrl->generic.type == CTRL_LISTBOX);
    c->ctrl->generic.handler(c->ctrl, d, d->data, EVENT_SELCHANGE);
}
- (BOOL)tableView:(NSTableView *)aTableView
    shouldEditTableColumn:(NSTableColumn *)aTableColumn
    row:(int)rowIndex
{
    return NO;			       /* no editing permitted */
}
- (void)listDoubleClicked:(id)sender
{
    struct fe_ctrl *c = find_widget(d, sender);
    assert(c && c->ctrl->generic.type == CTRL_LISTBOX);
    c->ctrl->generic.handler(c->ctrl, d, d->data, EVENT_ACTION);
}
- (void)dragListButton:(id)sender
{
    struct fe_ctrl *c = find_widget(d, sender);
    int direction, row, nrows;
    assert(c && c->ctrl->generic.type == CTRL_LISTBOX &&
	   c->ctrl->listbox.draglist);

    if (sender == c->button)
	direction = -1;		       /* up */
    else
	direction = +1;		       /* down */

    row = [c->tableview selectedRow];
    nrows = [c->tableview numberOfRows];

    if (row + direction < 0 || row + direction >= nrows) {
	NSBeep();
	return;
    }

    [[c->tableview dataSource] swap:row with:row+direction];
    [c->tableview reloadData];
    [c->tableview selectRow:row+direction byExtendingSelection:NO];

    c->ctrl->generic.handler(c->ctrl, d, d->data, EVENT_VALCHANGE);
}
@end

void create_ctrls(void *dv, NSView *parent, struct controlset *s,
		  int *minw, int *minh)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    int ccw[100];		       /* cumulative column widths */
    int cypos[100];
    int ncols;
    int wmin = 0, hmin = 0;
    int i, j, cw, ch;
    NSRect rect;
    NSFont *textviewfont = nil;
    int boxh = 0, boxw = 0;

    if (!s->boxname && s->boxtitle) {
        /* This controlset is a panel title. */

	NSTextField *tf;

	tf = [[NSTextField alloc] initWithFrame:NSMakeRect(0,0,1,1)];
	[tf setEditable:NO];
	[tf setSelectable:NO];
	[tf setBordered:NO];
	[tf setDrawsBackground:NO];
	[tf setStringValue:[NSString stringWithCString:s->boxtitle]];
	[tf sizeToFit];
	rect = [tf frame];
	[parent addSubview:tf];

	/*
	 * I'm going to store this NSTextField in the boxes tree,
	 * because I really can't face having a special tree234
	 * mapping controlsets to panel titles.
	 */
	add_box(d, s, tf);

	*minw = rect.size.width;
	*minh = rect.size.height;

	return;
    }

    if (*s->boxname) {
	/*
	 * Create an NSBox to contain this subset of controls.
	 */
	NSBox *box;
	NSRect tmprect;

	box = [[NSBox alloc] initWithFrame:NSMakeRect(0,0,1,1)];
	if (s->boxtitle)
	    [box setTitle:[NSString stringWithCString:s->boxtitle]];
	else
	    [box setTitlePosition:NSNoTitle];
	add_box(d, s, box);
	tmprect = [box frame];
	[box setContentViewMargins:NSMakeSize(20,20)];
	[box setFrameFromContentFrame:NSMakeRect(100,100,100,100)];
	rect = [box frame];
	[box setFrame:tmprect];
	boxh = (int)(rect.size.height - 100);
	boxw = (int)(rect.size.width - 100);
	[parent addSubview:box];

	if (s->boxtitle)
	    boxh += [[box titleFont] pointSize];

	/*
	 * All subsequent controls will be placed within this box.
	 */
	parent = box;
    }

    ncols = 1;
    ccw[0] = 0;
    ccw[1] = 100;
    cypos[0] = 0;

    /*
     * Now iterate through the controls themselves, create them,
     * and add their width and height to the overall width/height
     * calculation.
     */
    for (i = 0; i < s->ncontrols; i++) {
	union control *ctrl = s->ctrls[i];
	struct fe_ctrl *c;
	int colstart = COLUMN_START(ctrl->generic.column);
	int colspan = COLUMN_SPAN(ctrl->generic.column);
	int colend = colstart + colspan;
	int ytop, wthis;

        switch (ctrl->generic.type) {
          case CTRL_COLUMNS:
	    for (j = 1; j < ncols; j++)
		if (cypos[0] < cypos[j])
		    cypos[0] = cypos[j];

	    assert(ctrl->columns.ncols < lenof(ccw));

	    ccw[0] = 0;
	    for (j = 0; j < ctrl->columns.ncols; j++) {
		ccw[j+1] = ccw[j] + (ctrl->columns.percentages ?
				     ctrl->columns.percentages[j] : 100);
		cypos[j] = cypos[0];
	    }

	    ncols = ctrl->columns.ncols;

            continue;                  /* no actual control created */
          case CTRL_TABDELAY:
	    /*
	     * I'm currently uncertain that we can implement tab
	     * order in OS X.
	     */
            continue;                  /* no actual control created */
	}

	c = fe_ctrl_new(ctrl);
	add234(d->byctrl, c);

	cw = ch = 0;

        switch (ctrl->generic.type) {
          case CTRL_BUTTON:
          case CTRL_CHECKBOX:
	    {
		NSButton *b;

		b = [[MyButton alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
		[b setBezelStyle:NSRoundedBezelStyle];
		if (ctrl->generic.type == CTRL_CHECKBOX)
		    [b setButtonType:NSSwitchButton];
		[b setTitle:[NSString stringWithCString:ctrl->generic.label]];
		if (ctrl->button.isdefault)
		    [b setKeyEquivalent:@"\r"];
		else if (ctrl->button.iscancel)
		    [b setKeyEquivalent:@"\033"];
		[b sizeToFit];
		rect = [b frame];

		[parent addSubview:b];

		[b setTarget:d->rec];
		if (ctrl->generic.type == CTRL_CHECKBOX)
		    [b setAction:@selector(checkboxChanged:)];
		else
		    [b setAction:@selector(buttonPushed:)];
		add_widget(d, c, b);

		c->button = b;

		cw = rect.size.width;
		ch = rect.size.height;
	    }
	    break;
	  case CTRL_EDITBOX:
	    {
		int editp = ctrl->editbox.percentwidth;
		int labelp = editp == 100 ? 100 : 100 - editp;
		NSTextField *tf;
		NSComboBox *cb;

		tf = [[NSTextField alloc] initWithFrame:NSMakeRect(0,0,1,1)];
		[tf setEditable:NO];
		[tf setSelectable:NO];
		[tf setBordered:NO];
		[tf setDrawsBackground:NO];
		[tf setStringValue:[NSString
				    stringWithCString:ctrl->generic.label]];
		[tf sizeToFit];
		rect = [tf frame];
		[parent addSubview:tf];
		c->label = tf;

		cw = rect.size.width * 100 / labelp;
		ch = rect.size.height;

		if (ctrl->editbox.has_list) {
		    cb = [[NSComboBox alloc]
			  initWithFrame:NSMakeRect(0,0,1,1)];
		    [cb setStringValue:@"x"];
		    [cb sizeToFit];
		    rect = [cb frame];
		    [parent addSubview:cb];
		    c->combobox = cb;
		} else {
		    if (ctrl->editbox.password)
			tf = [NSSecureTextField alloc];
		    else
			tf = [NSTextField alloc];

		    tf = [tf initWithFrame:NSMakeRect(0,0,1,1)];
		    [tf setEditable:YES];
		    [tf setSelectable:YES];
		    [tf setBordered:YES];
		    [tf setStringValue:@"x"];
		    [tf sizeToFit];
		    rect = [tf frame];
		    [parent addSubview:tf];
		    c->editbox = tf;

		    [tf setDelegate:d->rec];
		    add_widget(d, c, tf);
		}

		if (editp == 100) {
		    /* the edit box and its label are vertically separated */
		    ch += VSPACING + rect.size.height;
		} else {
		    /* the edit box and its label are horizontally separated */
		    if (ch < rect.size.height)
			ch = rect.size.height;
		}

		if (cw < rect.size.width * 100 / editp)
		    cw = rect.size.width * 100 / editp;
	    }
	    break;
	  case CTRL_TEXT:
	    {
		NSTextView *tv;
		int testwid;

		if (!textviewfont) {
		    NSTextField *tf;
		    tf = [[NSTextField alloc] init];
		    textviewfont = [tf font];
		    [tf release];
		}

		testwid = (ccw[colend] - ccw[colstart]) * 3;

		tv = [[NSTextView alloc]
		      initWithFrame:NSMakeRect(0,0,testwid,1)];
		[tv setEditable:NO];
		[tv setSelectable:NO];
		//[tv setBordered:NO];
		[tv setDrawsBackground:NO];
		[tv setFont:textviewfont];
		[tv setString:
		 [NSString stringWithCString:ctrl->generic.label]];
		rect = [tv frame];
		[tv sizeToFit];
		[parent addSubview:tv];
		c->textview = tv;

		cw = rect.size.width;
		ch = rect.size.height;
	    }
	    break;
	  case CTRL_RADIO:
	    {
		NSTextField *tf;
		int j;

		if (ctrl->generic.label) {
		    tf = [[NSTextField alloc]
			  initWithFrame:NSMakeRect(0,0,1,1)];
		    [tf setEditable:NO];
		    [tf setSelectable:NO];
		    [tf setBordered:NO];
		    [tf setDrawsBackground:NO];
		    [tf setStringValue:
		     [NSString stringWithCString:ctrl->generic.label]];
		    [tf sizeToFit];
		    rect = [tf frame];
		    [parent addSubview:tf];
		    c->label = tf;

		    cw = rect.size.width;
		    ch = rect.size.height;
		} else {
		    cw = 0;
		    ch = -VSPACING;    /* compensate for next advance */
		}

		c->nradiobuttons = ctrl->radio.nbuttons;
		c->radiobuttons = snewn(ctrl->radio.nbuttons, NSButton *);

		for (j = 0; j < ctrl->radio.nbuttons; j++) {
		    NSButton *b;
		    int ncols;

		    b = [[MyButton alloc] initWithFrame:NSMakeRect(0,0,1,1)];
		    [b setBezelStyle:NSRoundedBezelStyle];
		    [b setButtonType:NSRadioButton];
		    [b setTitle:[NSString
				 stringWithCString:ctrl->radio.buttons[j]]];
		    [b sizeToFit];
		    rect = [b frame];
		    [parent addSubview:b];

		    c->radiobuttons[j] = b;

		    [b setTarget:d->rec];
		    [b setAction:@selector(radioChanged:)];
		    add_widget(d, c, b);

		    /*
		     * Add to the height every time we place a
		     * button in column 0.
		     */
		    if (j % ctrl->radio.ncolumns == 0) {
			ch += rect.size.height + VSPACING;
		    }

		    /*
		     * Add to the width by working out how many
		     * columns this button spans.
		     */
		    if (j == ctrl->radio.nbuttons - 1)
			ncols = (ctrl->radio.ncolumns -
				 (j % ctrl->radio.ncolumns));
		    else
			ncols = 1;

		    if (cw < rect.size.width * ctrl->radio.ncolumns / ncols)
			cw = rect.size.width * ctrl->radio.ncolumns / ncols;
		}
	    }
	    break;
	  case CTRL_FILESELECT:
	  case CTRL_FONTSELECT:
	    {
		NSTextField *tf;
		NSButton *b;
		int kh;

		tf = [[NSTextField alloc] initWithFrame:NSMakeRect(0,0,1,1)];
		[tf setEditable:NO];
		[tf setSelectable:NO];
		[tf setBordered:NO];
		[tf setDrawsBackground:NO];
		[tf setStringValue:[NSString
				    stringWithCString:ctrl->generic.label]];
		[tf sizeToFit];
		rect = [tf frame];
		[parent addSubview:tf];
		c->label = tf;

		cw = rect.size.width;
		ch = rect.size.height;

		tf = [NSTextField alloc];
		tf = [tf initWithFrame:NSMakeRect(0,0,1,1)];
		if (ctrl->generic.type == CTRL_FILESELECT) {
		    [tf setEditable:YES];
		    [tf setSelectable:YES];
		    [tf setBordered:YES];
		} else {
		    [tf setEditable:NO];
		    [tf setSelectable:NO];
		    [tf setBordered:NO];
		    [tf setDrawsBackground:NO];
		}
		[tf setStringValue:@"x"];
		[tf sizeToFit];
		rect = [tf frame];
		[parent addSubview:tf];
		c->editbox = tf;

		kh = rect.size.height;
		if (cw < rect.size.width * 4 / 3)
		    cw = rect.size.width * 4 / 3;

		b = [[MyButton alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
		[b setBezelStyle:NSRoundedBezelStyle];
		if (ctrl->generic.type == CTRL_FILESELECT)
		    [b setTitle:@"Browse..."];
		else
		    [b setTitle:@"Change..."];
		// [b setKeyEquivalent:somethingorother];
		// [b setTarget:somethingorother];
		// [b setAction:somethingorother];
		[b sizeToFit];
		rect = [b frame];
		[parent addSubview:b];

		c->button = b;

		if (kh < rect.size.height)
		    kh = rect.size.height;
		ch += VSPACING + kh;
		if (cw < rect.size.width * 4)
		    cw = rect.size.width * 4;
	    }
	    break;
	  case CTRL_LISTBOX:
	    {
		int listp = ctrl->listbox.percentwidth;
		int labelp = listp == 100 ? 100 : 100 - listp;
		NSTextField *tf;
		NSPopUpButton *pb;
		NSTableView *tv;
		NSScrollView *sv;

		if (ctrl->generic.label) {
		    tf = [[NSTextField alloc]
			  initWithFrame:NSMakeRect(0,0,1,1)];
		    [tf setEditable:NO];
		    [tf setSelectable:NO];
		    [tf setBordered:NO];
		    [tf setDrawsBackground:NO];
		    [tf setStringValue:
		     [NSString stringWithCString:ctrl->generic.label]];
		    [tf sizeToFit];
		    rect = [tf frame];
		    [parent addSubview:tf];
		    c->label = tf;

		    cw = rect.size.width;
		    ch = rect.size.height;
		} else {
		    cw = 0;
		    ch = -VSPACING;    /* compensate for next advance */
		}

		if (ctrl->listbox.height == 0) {
		    pb = [[NSPopUpButton alloc]
			  initWithFrame:NSMakeRect(0,0,1,1)];
		    [pb sizeToFit];
		    rect = [pb frame];
		    [parent addSubview:pb];
		    c->popupbutton = pb;

		    [pb setTarget:d->rec];
		    [pb setAction:@selector(popupMenuSelected:)];
		    add_widget(d, c, pb);
		} else {
		    assert(listp == 100);
		    if (ctrl->listbox.draglist) {
			int bi;

			listp = 75;

			for (bi = 0; bi < 2; bi++) {
			    NSButton *b;
			    b = [[MyButton alloc]
				 initWithFrame:NSMakeRect(0, 0, 1, 1)];
			    [b setBezelStyle:NSRoundedBezelStyle];
			    if (bi == 0)
				[b setTitle:@"Up"];
			    else
				[b setTitle:@"Down"];
			    [b sizeToFit];
			    rect = [b frame];
			    [parent addSubview:b];

			    if (bi == 0)
				c->button = b;
			    else
				c->button2 = b;

			    [b setTarget:d->rec];
			    [b setAction:@selector(dragListButton:)];
			    add_widget(d, c, b);

			    if (cw < rect.size.width * 4)
				cw = rect.size.width * 4;
			}
		    }

		    sv = [[NSScrollView alloc] initWithFrame:
			  NSMakeRect(20,20,10,10)];
		    [sv setBorderType:NSLineBorder];
		    tv = [[NSTableView alloc] initWithFrame:[sv frame]];
		    [[tv headerView] setFrame:NSMakeRect(0,0,0,0)];
		    [sv setDocumentView:tv];
		    [parent addSubview:sv];
		    [sv setHasVerticalScroller:YES];
		    [sv setAutohidesScrollers:YES];
		    [tv setAllowsColumnReordering:NO];
		    [tv setAllowsColumnResizing:NO];
		    [tv setAllowsMultipleSelection:ctrl->listbox.multisel];
		    [tv setAllowsEmptySelection:YES];
		    [tv setAllowsColumnSelection:YES];
		    [tv setDataSource:[[MyTableSource alloc] init]];
		    rect = [tv frame];
		    /*
		     * For some reason this consistently comes out
		     * one short. Add one.
		     */
		    rect.size.height = (ctrl->listbox.height+1)*[tv rowHeight];
		    [sv setFrame:rect];
		    c->tableview = tv;
		    c->scrollview = sv;

		    [tv setDelegate:d->rec];
		    [tv setTarget:d->rec];
		    [tv setDoubleAction:@selector(listDoubleClicked:)];
		    add_widget(d, c, tv);
		}

		if (c->tableview) {
		    int ncols, *percentages;
		    int hundred = 100;

		    if (ctrl->listbox.ncols) {
			ncols = ctrl->listbox.ncols;
			percentages = ctrl->listbox.percentages;
		    } else {
			ncols = 1;
			percentages = &hundred;
		    }

		    for (j = 0; j < ncols; j++) {
			NSTableColumn *col;

			col = [[NSTableColumn alloc] initWithIdentifier:
			       [NSNumber numberWithInt:j]];
			[c->tableview addTableColumn:col];
		    }
		}

		if (labelp == 100) {
		    /* the list and its label are vertically separated */
		    ch += VSPACING + rect.size.height;
		} else {
		    /* the list and its label are horizontally separated */
		    if (ch < rect.size.height)
			ch = rect.size.height;
		}

		if (cw < rect.size.width * 100 / listp)
		    cw = rect.size.width * 100 / listp;
	    }
	    break;
	}

	/*
	 * Update the width and height data for the control we've
	 * just created.
	 */
	ytop = 0;

	for (j = colstart; j < colend; j++) {
	    if (ytop < cypos[j])
		ytop = cypos[j];
	}

	for (j = colstart; j < colend; j++)
	    cypos[j] = ytop + ch + VSPACING;

	if (hmin < ytop + ch)
	    hmin = ytop + ch;

	wthis = (cw + HSPACING) * 100 / (ccw[colend] - ccw[colstart]);
	wthis -= HSPACING;

	if (wmin < wthis)
	    wmin = wthis;
    }

    if (*s->boxname) {
	/*
	 * Add a bit to the width and height for the box.
	 */
	wmin += boxw;
	hmin += boxh;
    }

    //printf("For controlset %s/%s, returning w=%d h=%d\n",
    //       s->pathname, s->boxname, wmin, hmin);
    *minw = wmin;
    *minh = hmin;
}

int place_ctrls(void *dv, struct controlset *s, int leftx, int topy,
		int width)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    int ccw[100];		       /* cumulative column widths */
    int cypos[100];
    int ncols;
    int i, j, ret;
    int boxh = 0, boxw = 0;

    if (!s->boxname && s->boxtitle) {
        /* Size and place the panel title. */

	NSTextField *tf = find_box(d, s);
	NSRect rect;

	rect = [tf frame];
	[tf setFrame:NSMakeRect(leftx, topy-rect.size.height,
				width, rect.size.height)];
	return rect.size.height;
    }

    if (*s->boxname) {
	NSRect rect, tmprect;
	NSBox *box = find_box(d, s);

	assert(box != NULL);
	tmprect = [box frame];
	[box setFrameFromContentFrame:NSMakeRect(100,100,100,100)];
	rect = [box frame];
	[box setFrame:tmprect];
	boxw = rect.size.width - 100;
	boxh = rect.size.height - 100;
	if (s->boxtitle)
	    boxh += [[box titleFont] pointSize];
	topy -= boxh;
	width -= boxw;
    }

    ncols = 1;
    ccw[0] = 0;
    ccw[1] = 100;
    cypos[0] = topy;
    ret = 0;

    /*
     * Now iterate through the controls themselves, placing them
     * appropriately.
     */
    for (i = 0; i < s->ncontrols; i++) {
	union control *ctrl = s->ctrls[i];
	struct fe_ctrl *c;
	int colstart = COLUMN_START(ctrl->generic.column);
	int colspan = COLUMN_SPAN(ctrl->generic.column);
	int colend = colstart + colspan;
	int xthis, ythis, wthis, ch;
	NSRect rect;

        switch (ctrl->generic.type) {
          case CTRL_COLUMNS:
	    for (j = 1; j < ncols; j++)
		if (cypos[0] > cypos[j])
		    cypos[0] = cypos[j];

	    assert(ctrl->columns.ncols < lenof(ccw));

	    ccw[0] = 0;
	    for (j = 0; j < ctrl->columns.ncols; j++) {
		ccw[j+1] = ccw[j] + (ctrl->columns.percentages ?
				     ctrl->columns.percentages[j] : 100);
		cypos[j] = cypos[0];
	    }

	    ncols = ctrl->columns.ncols;

            continue;                  /* no actual control created */
          case CTRL_TABDELAY:
            continue;                  /* nothing to do here, move along */
	}

	c = fe_ctrl_byctrl(d, ctrl);

	ch = 0;
	ythis = topy;

	for (j = colstart; j < colend; j++) {
	    if (ythis > cypos[j])
		ythis = cypos[j];
	}

	xthis = (width + HSPACING) * ccw[colstart] / 100;
	wthis = (width + HSPACING) * ccw[colend] / 100 - HSPACING - xthis;
	xthis += leftx;

        switch (ctrl->generic.type) {
          case CTRL_BUTTON:
	  case CTRL_CHECKBOX:
	    rect = [c->button frame];
	    [c->button setFrame:NSMakeRect(xthis,ythis-rect.size.height,wthis,
					   rect.size.height)];
	    ch = rect.size.height;
	    break;
	  case CTRL_EDITBOX:
	    {
		int editp = ctrl->editbox.percentwidth;
		int labelp = editp == 100 ? 100 : 100 - editp;
		int lheight, theight, rheight, ynext, editw;
		NSControl *edit = (c->editbox ? c->editbox : c->combobox);

	 	rect = [c->label frame];
		lheight = rect.size.height;
		rect = [edit frame];
		theight = rect.size.height;

		if (editp == 100)
		    rheight = lheight;
		else
		    rheight = (lheight < theight ? theight : lheight);

		[c->label setFrame:
		 NSMakeRect(xthis, ythis-(rheight+lheight)/2,
			    (wthis + HSPACING) * labelp / 100 - HSPACING,
			    lheight)];
		if (editp == 100) {
		    ynext = ythis - rheight - VSPACING;
		    rheight = theight;
		} else {
		    ynext = ythis;
		}

		editw = (wthis + HSPACING) * editp / 100 - HSPACING;

		[edit setFrame:
		 NSMakeRect(xthis+wthis-editw, ynext-(rheight+theight)/2,
			    editw, theight)];

		ch = (ythis - ynext) + theight;
	    }
	    break;
          case CTRL_TEXT:
	    [c->textview setFrame:NSMakeRect(xthis, 0, wthis, 1)];
	    [c->textview sizeToFit];
	    rect = [c->textview frame];
	    [c->textview setFrame:NSMakeRect(xthis, ythis-rect.size.height,
					     wthis, rect.size.height)];
	    ch = rect.size.height;
	    break;
	  case CTRL_RADIO:
	    {
		int j, ynext;

		if (c->label) {
		    rect = [c->label frame];
		    [c->label setFrame:NSMakeRect(xthis,ythis-rect.size.height,
						  wthis,rect.size.height)];
		    ynext = ythis - rect.size.height - VSPACING;
		} else
		    ynext = ythis;

		for (j = 0; j < ctrl->radio.nbuttons; j++) {
		    int col = j % ctrl->radio.ncolumns;
		    int ncols;
		    int lx,rx;

		    if (j == ctrl->radio.nbuttons - 1)
			ncols = ctrl->radio.ncolumns - col;
		    else
			ncols = 1;

		    lx = (wthis + HSPACING) * col / ctrl->radio.ncolumns;
		    rx = ((wthis + HSPACING) *
			  (col+ncols) / ctrl->radio.ncolumns) - HSPACING;

		    /*
		     * Set the frame size.
		     */
		    rect = [c->radiobuttons[j] frame];
		    [c->radiobuttons[j] setFrame:
		     NSMakeRect(lx+xthis, ynext-rect.size.height,
				rx-lx, rect.size.height)];

		    /*
		     * Advance to next line if we're in the last
		     * column.
		     */
		    if (col + ncols == ctrl->radio.ncolumns)
			ynext -= rect.size.height + VSPACING;
		}
		ch = (ythis - ynext) - VSPACING;
	    }
	    break;
	  case CTRL_FILESELECT:
	  case CTRL_FONTSELECT:
	    {
		int ynext, eh, bh, th, mx;

		rect = [c->label frame];
		[c->label setFrame:NSMakeRect(xthis,ythis-rect.size.height,
					      wthis,rect.size.height)];
		ynext = ythis - rect.size.height - VSPACING;

		rect = [c->editbox frame];
		eh = rect.size.height;
		rect = [c->button frame];
		bh = rect.size.height;
		th = (eh > bh ? eh : bh);

		mx = (wthis + HSPACING) * 3 / 4 - HSPACING;

		[c->editbox setFrame:
		 NSMakeRect(xthis, ynext-(th+eh)/2, mx, eh)];
		[c->button setFrame:
		 NSMakeRect(xthis+mx+HSPACING, ynext-(th+bh)/2,
			    wthis-mx-HSPACING, bh)];

		ch = (ythis - ynext) + th + VSPACING;
	    }
	    break;
	  case CTRL_LISTBOX:
	    {
		int listp = ctrl->listbox.percentwidth;
		int labelp = listp == 100 ? 100 : 100 - listp;
		int lheight, theight, rheight, ynext, listw, xlist;
		NSControl *list = (c->scrollview ? (id)c->scrollview :
				   (id)c->popupbutton);

		if (ctrl->listbox.draglist) {
		    assert(listp == 100);
		    listp = 75;
		}

		rect = [list frame];
		theight = rect.size.height;

		if (c->label) {
		    rect = [c->label frame];
		    lheight = rect.size.height;

		    if (labelp == 100)
			rheight = lheight;
		    else
			rheight = (lheight < theight ? theight : lheight);

		    [c->label setFrame:
		     NSMakeRect(xthis, ythis-(rheight+lheight)/2,
				(wthis + HSPACING) * labelp / 100 - HSPACING,
				lheight)];
		    if (labelp == 100) {
			ynext = ythis - rheight - VSPACING;
			rheight = theight;
		    } else {
			ynext = ythis;
		    }
		} else {
		    ynext = ythis;
		    rheight = theight;
		}

		listw = (wthis + HSPACING) * listp / 100 - HSPACING;

		if (labelp == 100)
		    xlist = xthis;
		else
		    xlist = xthis+wthis-listw;

		[list setFrame: NSMakeRect(xlist, ynext-(rheight+theight)/2,
					   listw, theight)];

		/*
		 * Size the columns for the table view.
		 */
		if (c->tableview) {
		    int ncols, *percentages;
		    int hundred = 100;
		    int cpercent = 0, cpixels = 0;
		    NSArray *cols;

		    if (ctrl->listbox.ncols) {
			ncols = ctrl->listbox.ncols;
			percentages = ctrl->listbox.percentages;
		    } else {
			ncols = 1;
			percentages = &hundred;
		    }

		    cols = [c->tableview tableColumns];

		    for (j = 0; j < ncols; j++) {
			NSTableColumn *col = [cols objectAtIndex:j];
			int newcpixels;

			cpercent += percentages[j];
			newcpixels = listw * cpercent / 100;
			[col setWidth:newcpixels-cpixels];
			cpixels = newcpixels;
		    }
		}

		ch = (ythis - ynext) + theight;

		if (c->button) {
		    int b2height, centre;
		    int bx, bw;

		    /*
		     * Place the Up and Down buttons for a drag list.
		     */
		    assert(c->button2);

		    rect = [c->button frame];
		    b2height = VSPACING + 2 * rect.size.height;

		    centre = ynext - rheight/2;

		    bx = (wthis + HSPACING) * 3 / 4;
		    bw = wthis - bx;
		    bx += leftx;

		    [c->button setFrame:
		     NSMakeRect(bx, centre+b2height/2-rect.size.height,
				bw, rect.size.height)];
		    [c->button2 setFrame:
		     NSMakeRect(bx, centre-b2height/2,
				bw, rect.size.height)];
		}
	    }
	    break;
	}

	for (j = colstart; j < colend; j++)
	    cypos[j] = ythis - ch - VSPACING;
	if (ret < topy - (ythis - ch))
	    ret = topy - (ythis - ch);
    }

    if (*s->boxname) {
	NSBox *box = find_box(d, s);
	assert(box != NULL);
	[box sizeToFit];

	if (s->boxtitle) {
	    NSRect rect = [box frame];
	    rect.size.height += [[box titleFont] pointSize];
	    [box setFrame:rect];
	}

	ret += boxh;
    }

    //printf("For controlset %s/%s, returning ret=%d\n",
    //       s->pathname, s->boxname, ret);
    return ret;
}

void select_panel(void *dv, struct controlbox *b, const char *name)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    int i, j, hidden;
    struct controlset *s;
    union control *ctrl;
    struct fe_ctrl *c;
    NSBox *box;

    for (i = 0; i < b->nctrlsets; i++) {
	s = b->ctrlsets[i];

	if (*s->pathname) {
	    hidden = !strcmp(s->pathname, name) ? NO : YES;

	    if ((box = find_box(d, s)) != NULL) {
		[box setHidden:hidden];
	    } else {
		for (j = 0; j < s->ncontrols; j++) {
		    ctrl = s->ctrls[j];
		    c = fe_ctrl_byctrl(d, ctrl);

		    if (!c)
			continue;

		    if (c->label)
			[c->label setHidden:hidden];
		    if (c->button)
			[c->button setHidden:hidden];
		    if (c->button2)
			[c->button2 setHidden:hidden];
		    if (c->editbox)
			[c->editbox setHidden:hidden];
		    if (c->combobox)
			[c->combobox setHidden:hidden];
		    if (c->textview)
			[c->textview setHidden:hidden];
		    if (c->tableview)
			[c->tableview setHidden:hidden];
		    if (c->scrollview)
			[c->scrollview setHidden:hidden];
		    if (c->popupbutton)
			[c->popupbutton setHidden:hidden];
		    if (c->radiobuttons) {
			int j;
			for (j = 0; j < c->nradiobuttons; j++)
			    [c->radiobuttons[j] setHidden:hidden];
		    }
		    break;
		}
	    }
	}
    }
}

void dlg_radiobutton_set(union control *ctrl, void *dv, int whichbutton)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    struct fe_ctrl *c = fe_ctrl_byctrl(d, ctrl);
    int j;

    assert(c->radiobuttons);
    for (j = 0; j < c->nradiobuttons; j++)
	[c->radiobuttons[j] setState:
	 (j == whichbutton ? NSOnState : NSOffState)];
}

int dlg_radiobutton_get(union control *ctrl, void *dv)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    struct fe_ctrl *c = fe_ctrl_byctrl(d, ctrl);
    int j;

    assert(c->radiobuttons);
    for (j = 0; j < c->nradiobuttons; j++)
	if ([c->radiobuttons[j] state] == NSOnState)
	    return j;

    return 0;			       /* should never reach here */
}

void dlg_checkbox_set(union control *ctrl, void *dv, int checked)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    struct fe_ctrl *c = fe_ctrl_byctrl(d, ctrl);

    assert(c->button);
    [c->button setState:(checked ? NSOnState : NSOffState)];
}

int dlg_checkbox_get(union control *ctrl, void *dv)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    struct fe_ctrl *c = fe_ctrl_byctrl(d, ctrl);

    assert(c->button);
    return ([c->button state] == NSOnState);
}

void dlg_editbox_set(union control *ctrl, void *dv, char const *text)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    struct fe_ctrl *c = fe_ctrl_byctrl(d, ctrl);

    if (c->editbox) {
	[c->editbox setStringValue:[NSString stringWithCString:text]];
    } else {
	assert(c->combobox);
	[c->combobox setStringValue:[NSString stringWithCString:text]];
    }
}

void dlg_editbox_get(union control *ctrl, void *dv, char *buffer, int length)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    struct fe_ctrl *c = fe_ctrl_byctrl(d, ctrl);
    NSString *str;

    if (c->editbox) {
	str = [c->editbox stringValue];
    } else {
	assert(c->combobox);
	str = [c->combobox stringValue];
    }
    if (!str)
	str = @"";

    /* The length parameter to this method doesn't include a trailing NUL */
    [str getCString:buffer maxLength:length-1];
}

void dlg_listbox_clear(union control *ctrl, void *dv)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    struct fe_ctrl *c = fe_ctrl_byctrl(d, ctrl);

    if (c->tableview) {
	[[c->tableview dataSource] clear];
	[c->tableview reloadData];
    } else {
	[c->popupbutton removeAllItems];
    }
}

void dlg_listbox_del(union control *ctrl, void *dv, int index)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    struct fe_ctrl *c = fe_ctrl_byctrl(d, ctrl);

    if (c->tableview) {
	[[c->tableview dataSource] removestr:index];
	[c->tableview reloadData];
    } else {
	[c->popupbutton removeItemAtIndex:index];
    }
}

void dlg_listbox_addwithid(union control *ctrl, void *dv,
			   char const *text, int id)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    struct fe_ctrl *c = fe_ctrl_byctrl(d, ctrl);

    if (c->tableview) {
	[[c->tableview dataSource] add:text withId:id];
	[c->tableview reloadData];
    } else {
	[c->popupbutton addItemWithTitle:[NSString stringWithCString:text]];
	[[c->popupbutton lastItem] setTag:id];
    }
}

void dlg_listbox_add(union control *ctrl, void *dv, char const *text)
{
    dlg_listbox_addwithid(ctrl, dv, text, -1);
}

int dlg_listbox_getid(union control *ctrl, void *dv, int index)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    struct fe_ctrl *c = fe_ctrl_byctrl(d, ctrl);

    if (c->tableview) {
	return [[c->tableview dataSource] getid:index];
    } else {
	return [[c->popupbutton itemAtIndex:index] tag];
    }
}

int dlg_listbox_index(union control *ctrl, void *dv)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    struct fe_ctrl *c = fe_ctrl_byctrl(d, ctrl);

    if (c->tableview) {
	return [c->tableview selectedRow];
    } else {
	return [c->popupbutton indexOfSelectedItem];
    }
}

int dlg_listbox_issel(union control *ctrl, void *dv, int index)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    struct fe_ctrl *c = fe_ctrl_byctrl(d, ctrl);

    if (c->tableview) {
	return [c->tableview isRowSelected:index];
    } else {
	return [c->popupbutton indexOfSelectedItem] == index;
    }
}

void dlg_listbox_select(union control *ctrl, void *dv, int index)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    struct fe_ctrl *c = fe_ctrl_byctrl(d, ctrl);

    if (c->tableview) {
	[c->tableview selectRow:index byExtendingSelection:NO];
    } else {
	[c->popupbutton selectItemAtIndex:index];
    }
}

void dlg_text_set(union control *ctrl, void *dv, char const *text)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    struct fe_ctrl *c = fe_ctrl_byctrl(d, ctrl);

    assert(c->textview);
    [c->textview setString:[NSString stringWithCString:text]];
}

void dlg_label_change(union control *ctrl, void *dlg, char const *text)
{
    /*
     * This function is currently only used by the config box to
     * switch the labels on the host and port boxes between serial
     * and network modes. Since OS X does not (yet?) have a serial
     * back end, this function can safely do nothing for the
     * moment.
     */
}

void dlg_filesel_set(union control *ctrl, void *dv, Filename fn)
{
    /* FIXME */
}

void dlg_filesel_get(union control *ctrl, void *dv, Filename *fn)
{
    /* FIXME */
}

void dlg_fontsel_set(union control *ctrl, void *dv, FontSpec fn)
{
    /* FIXME */
}

void dlg_fontsel_get(union control *ctrl, void *dv, FontSpec *fn)
{
    /* FIXME */
}

void dlg_update_start(union control *ctrl, void *dv)
{
    /* FIXME */
}

void dlg_update_done(union control *ctrl, void *dv)
{
    /* FIXME */
}

void dlg_set_focus(union control *ctrl, void *dv)
{
    /* FIXME */
}

union control *dlg_last_focused(union control *ctrl, void *dv)
{
    return NULL; /* FIXME */
}

void dlg_beep(void *dv)
{
    NSBeep();
}

void dlg_error_msg(void *dv, char *msg)
{
    /* FIXME */
}

void dlg_end(void *dv, int value)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    [d->target performSelector:d->action
     withObject:[NSNumber numberWithInt:value]];
}

void dlg_coloursel_start(union control *ctrl, void *dv,
			 int r, int g, int b)
{
    /* FIXME */
}

int dlg_coloursel_results(union control *ctrl, void *dv,
			  int *r, int *g, int *b)
{
    return 0; /* FIXME */
}

void dlg_refresh(union control *ctrl, void *dv)
{
    struct fe_dlg *d = (struct fe_dlg *)dv;
    struct fe_ctrl *c;

    if (ctrl) {
	if (ctrl->generic.handler != NULL)
	    ctrl->generic.handler(ctrl, d, d->data, EVENT_REFRESH);
    } else {
	int i;

	for (i = 0; (c = index234(d->byctrl, i)) != NULL; i++) {
	    assert(c->ctrl != NULL);
	    if (c->ctrl->generic.handler != NULL)
		c->ctrl->generic.handler(c->ctrl, d,
					 d->data, EVENT_REFRESH);
	}
    }
}
