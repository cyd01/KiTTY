/*
 * osxsel.m: OS X implementation of the front end interface to uxsel.
 */

#import <Cocoa/Cocoa.h>
#include <unistd.h>
#include "putty.h"
#include "osxclass.h"

/*
 * The unofficial Cocoa FAQ at
 *
 *   http://www.alastairs-place.net/cocoa/faq.txt
 * 
 * says that Cocoa has the native ability to be given an fd and
 * tell you when it becomes readable, but cannot tell you when it
 * becomes _writable_. This is unacceptable to PuTTY, which depends
 * for correct functioning on being told both. Therefore, I can't
 * use the Cocoa native mechanism.
 * 
 * Instead, I'm going to resort to threads. I start a second thread
 * whose job is to do selects. At the termination of every select,
 * it posts a Cocoa event into the main thread's event queue, so
 * that the main thread gets select results interleaved with other
 * GUI operations. Communication from the main thread _to_ the
 * select thread is performed by writing to a pipe whose other end
 * is one of the file descriptors being selected on. (This is the
 * only sensible way, because we have to be able to interrupt a
 * select in order to provide a new fd list.)
 */

/*
 * In more detail, the select thread must:
 * 
 *  - start off by listening to _just_ the pipe, waiting to be told
 *    to begin a select.
 * 
 *  - when it receives the `start' command, it should read the
 *    shared uxsel data (which is protected by a mutex), set up its
 *    select, and begin it.
 * 
 *  - when the select terminates, it should write the results
 *    (perhaps minus the inter-thread pipe if it's there) into
 *    shared memory and dispatch a GUI event to let the main thread
 *    know.
 * 
 *  - the main thread will then think about it, do some processing,
 *    and _then_ send a command saying `now restart select'. Before
 *    sending that command it might easily have tinkered with the
 *    uxsel structures, which is why it waited before sending it.
 * 
 *  - EOF on the inter-thread pipe, of course, means the process
 *    has finished completely, so the select thread terminates.
 * 
 *  - The main thread may wish to adjust the uxsel settings in the
 *    middle of a select. In this situation it first writes the new
 *    data to the shared memory area, then notifies the select
 *    thread by writing to the inter-thread pipe.
 * 
 * So the upshot is that the sequence of operations performed in
 * the select thread must be:
 * 
 *  - read a byte from the pipe (which may block)
 * 
 *  - read the shared uxsel data and perform a select
 * 
 *  - notify the main thread of interesting select results (if any)
 * 
 *  - loop round again from the top.
 * 
 * This is sufficient. Notifying the select thread asynchronously
 * by writing to the pipe will cause its select to terminate and
 * another to begin immediately without blocking. If the select
 * thread's select terminates due to network data, its subsequent
 * pipe read will block until the main thread is ready to let it
 * loose again.
 */

static int osxsel_pipe[2];

static NSLock *osxsel_inlock;
static fd_set osxsel_rfds_in;
static fd_set osxsel_wfds_in;
static fd_set osxsel_xfds_in;
static int osxsel_inmax;

static NSLock *osxsel_outlock;
static fd_set osxsel_rfds_out;
static fd_set osxsel_wfds_out;
static fd_set osxsel_xfds_out;
static int osxsel_outmax;

static int inhibit_start_select;

/*
 * NSThread requires an object method as its thread procedure, so
 * here I define a trivial holding class.
 */
@class OSXSel;
@interface OSXSel : NSObject
{
}
- (void)runThread:(id)arg;
@end
@implementation OSXSel
- (void)runThread:(id)arg
{
    char c;
    fd_set r, w, x;
    int n, ret;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    while (1) {
	/*
	 * Read one byte from the pipe.
	 */
	ret = read(osxsel_pipe[0], &c, 1);

	if (ret <= 0)
	    return;		       /* terminate the thread */

	/*
	 * Now set up the select data.
	 */
	[osxsel_inlock lock];
	memcpy(&r, &osxsel_rfds_in, sizeof(fd_set));
	memcpy(&w, &osxsel_wfds_in, sizeof(fd_set));
	memcpy(&x, &osxsel_xfds_in, sizeof(fd_set));
	n = osxsel_inmax;
	[osxsel_inlock unlock];
	FD_SET(osxsel_pipe[0], &r);
	if (n < osxsel_pipe[0]+1)
	    n = osxsel_pipe[0]+1;

	/*
	 * Perform the select.
	 */
	ret = select(n, &r, &w, &x, NULL);

	/*
	 * Detect the one special case in which the only
	 * interesting fd was the inter-thread pipe. In that
	 * situation only we are interested - the main thread will
	 * not be!
	 */
	if (ret == 1 && FD_ISSET(osxsel_pipe[0], &r))
	    continue;		       /* just loop round again */

	/*
	 * Write the select results to shared data.
	 * 
	 * I _think_ we don't need this data to be lock-protected:
	 * it won't be read by the main thread until after we send
	 * a message indicating that we've finished writing it, and
	 * we won't start another select (hence potentially writing
	 * it again) until the main thread notifies us in return.
	 * 
	 * However, I'm scared of multithreading and not totally
	 * convinced of my reasoning, so I'm going to lock it
	 * anyway.
	 */
	[osxsel_outlock lock];
	memcpy(&osxsel_rfds_out, &r, sizeof(fd_set));
	memcpy(&osxsel_wfds_out, &w, sizeof(fd_set));
	memcpy(&osxsel_xfds_out, &x, sizeof(fd_set));
	osxsel_outmax = n;
	[osxsel_outlock unlock];

	/*
	 * Post a message to the main thread's message queue
	 * telling it that select data is available.
	 */
	[NSApp postEvent:[NSEvent otherEventWithType:NSApplicationDefined
			  location:NSMakePoint(0,0)
			  modifierFlags:0
			  timestamp:0
			  windowNumber:0
			  context:nil
			  subtype:0
			  data1:0
			  data2:0]
	 atStart:NO];
    }

    [pool release];
}
@end

void osxsel_init(void)
{
    uxsel_init();

    if (pipe(osxsel_pipe) < 0) {
	fatalbox("Unable to set up inter-thread pipe for select");
    }
    [NSThread detachNewThreadSelector:@selector(runThread:)
	toTarget:[[[OSXSel alloc] init] retain] withObject:nil];
    /*
     * Also initialise (i.e. clear) the input fd_sets. Need not
     * start a select just yet - the select thread will block until
     * we have at least one fd for it!
     */
    FD_ZERO(&osxsel_rfds_in);
    FD_ZERO(&osxsel_wfds_in);
    FD_ZERO(&osxsel_xfds_in);
    osxsel_inmax = 0;
    /*
     * Initialise the mutex locks used to protect the data passed
     * between threads.
     */
    osxsel_inlock = [[[NSLock alloc] init] retain];
    osxsel_outlock = [[[NSLock alloc] init] retain];
}

static void osxsel_start_select(void)
{
    char c = 'g';		       /* for `Go!' :-) but it's never used */

    if (!inhibit_start_select)
	write(osxsel_pipe[1], &c, 1);
}

int uxsel_input_add(int fd, int rwx)
{
    /*
     * Add the new fd to the appropriate input fd_sets, then write
     * to the inter-thread pipe.
     */
    [osxsel_inlock lock];
    if (rwx & 1)
	FD_SET(fd, &osxsel_rfds_in);
    else
	FD_CLR(fd, &osxsel_rfds_in);
    if (rwx & 2)
	FD_SET(fd, &osxsel_wfds_in);
    else
	FD_CLR(fd, &osxsel_wfds_in);
    if (rwx & 4)
	FD_SET(fd, &osxsel_xfds_in);
    else
	FD_CLR(fd, &osxsel_xfds_in);
    if (osxsel_inmax < fd+1)
	osxsel_inmax = fd+1;
    [osxsel_inlock unlock];
    osxsel_start_select();

    /*
     * We must return an `id' which will be passed back to us at
     * the time of uxsel_input_remove. Since we have no need to
     * store ids in that sense, we might as well go with the fd
     * itself.
     */
    return fd;
}

void uxsel_input_remove(int id)
{
    /*
     * Remove the fd from all the input fd_sets. In this
     * implementation, the simplest way to do that is to call
     * uxsel_input_add with rwx==0!
     */
    uxsel_input_add(id, 0);
}

/*
 * Function called in the main thread to process results. It will
 * have to read the output fd_sets, go through them, call back to
 * uxsel with the results, and then write to the inter-thread pipe.
 * 
 * This function will have to be called from an event handler in
 * osxmain.m, which will therefore necessarily contain a small part
 * of this mechanism (along with calling osxsel_init).
 */
void osxsel_process_results(void)
{
    int i;

    /*
     * We must write to the pipe to start a fresh select _even if_
     * there were no changes. So for efficiency, we set a flag here
     * which inhibits uxsel_input_{add,remove} from writing to the
     * pipe; then once we finish processing, we clear the flag
     * again and write a single byte ourselves. It's cleaner,
     * because it wakes up the select thread fewer times.
     */
    inhibit_start_select = TRUE;

    [osxsel_outlock lock];

    for (i = 0; i < osxsel_outmax; i++) {
	if (FD_ISSET(i, &osxsel_xfds_out))
	    select_result(i, 4);
    }
    for (i = 0; i < osxsel_outmax; i++) {
	if (FD_ISSET(i, &osxsel_rfds_out))
	    select_result(i, 1);
    }
    for (i = 0; i < osxsel_outmax; i++) {
	if (FD_ISSET(i, &osxsel_wfds_out))
	    select_result(i, 2);
    }

    [osxsel_outlock unlock];

    inhibit_start_select = FALSE;
    osxsel_start_select();
}
