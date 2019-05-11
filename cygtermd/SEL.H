/*
 * sel.h: subsystem to manage the grubby details of a select loop,
 * buffering data to write, and performing the actual writes and
 * reads.
 */

#ifndef FIXME_SEL_H
#define FIXME_SEL_H

typedef struct sel sel;
typedef struct sel_wfd sel_wfd;
typedef struct sel_rfd sel_rfd;

/*
 * Callback called when some data is written to a wfd. "bufsize"
 * is the remaining quantity of data buffered in that wfd.
 */
typedef void (*sel_written_fn_t)(sel_wfd *wfd, size_t bufsize);

/*
 * Callback called when an error occurs on a wfd, preventing
 * further writing to it. "error" is the errno value.
 */
typedef void (*sel_writeerr_fn_t)(sel_wfd *wfd, int error);

/*
 * Callback called when some data is read from an rfd. On EOF,
 * this will be called with len==0.
 */
typedef void (*sel_readdata_fn_t)(sel_rfd *rfd, void *data, size_t len);

/*
 * Callback called when an error occurs on an rfd, preventing
 * further reading from it. "error" is the errno value.
 */
typedef void (*sel_readerr_fn_t)(sel_rfd *rfd, int error);

/*
 * Create a sel structure, which will oversee a select loop.
 * 
 * "ctx" is user-supplied data stored in the sel structure; it can
 * be read and written with sel_get_ctx() and sel_set_ctx().
 */
sel *sel_new(void *ctx);

/*
 * Add a new fd for writing. Returns a sel_wfd which identifies
 * that fd in the sel structure, e.g. for putting data into its
 * output buffer.
 * 
 * "ctx" is user-supplied data stored in the sel structure; it can
 * be read and written with sel_wfd_get_ctx() and sel_wfd_set_ctx().
 * 
 * "written" and "writeerr" are called from the event loop when
 * things happen.
 *
 * The fd passed in can be -1, in which case it will be assumed to
 * be unwritable at all times. An actual fd can be passed in later
 * using sel_wfd_setfd.
 */
sel_wfd *sel_wfd_add(sel *sel, int fd,
		     sel_written_fn_t written, sel_writeerr_fn_t writeerr,
		     void *ctx);

/*
 * Add a new fd for reading. Returns a sel_rfd which identifies
 * that fd in the sel structure.
 * 
 * "ctx" is user-supplied data stored in the sel structure; it can
 * be read and written with sel_rfd_get_ctx() and sel_rfd_set_ctx().
 * 
 * "readdata" and "readerr" are called from the event loop when
 * things happen. "ctx" is passed to both of them.
 */
sel_rfd *sel_rfd_add(sel *sel, int fd,
		     sel_readdata_fn_t readdata, sel_readerr_fn_t readerr,
		     void *ctx);

/*
 * Write data into the output buffer of a wfd. Returns the new
 * size of the output buffer. (You can call it with len==0 if you
 * just want to know the buffer size; in that situation data==NULL
 * is also safe.)
 */
size_t sel_write(sel_wfd *wfd, const void *data, size_t len);

/*
 * Freeze and unfreeze an rfd. When frozen, sel will temporarily
 * not attempt to read from it, but all its state is retained so
 * it can be conveniently unfrozen later. (You might use this
 * facility, for instance, if what you were doing with the
 * incoming data could only accept it at a certain rate: freeze
 * the rfd when you've got lots of backlog, and unfreeze it again
 * when things get calmer.)
 */
void sel_rfd_freeze(sel_rfd *rfd);
void sel_rfd_unfreeze(sel_rfd *rfd);

/*
 * Delete a wfd structure from its containing sel. Returns the
 * underlying fd, which the client may now consider itself to own
 * once more.
 */
int sel_wfd_delete(sel_wfd *wfd);

/*
 * Delete an rfd structure from its containing sel. Returns the
 * underlying fd, which the client may now consider itself to own
 * once more.
 */
int sel_rfd_delete(sel_rfd *rfd);

/*
 * NOT IMPLEMENTED YET: useful functions here might be ones which
 * enumerated all the wfds/rfds in a sel structure in some
 * fashion, so you could go through them and remove them all while
 * doing sensible things to them. Or, at the very least, just
 * return an arbitrary one of the wfds/rfds.
 */

/*
 * Free a sel structure and all its remaining wfds and rfds.
 */
void sel_free(sel *sel);

/*
 * Read and write the ctx parameters in sel, sel_wfd and sel_rfd.
 */
void *sel_get_ctx(sel *sel);
void sel_set_ctx(sel *sel, void *ctx);
void *sel_wfd_get_ctx(sel_wfd *wfd);
void sel_wfd_set_ctx(sel_wfd *wfd, void *ctx);
void *sel_rfd_get_ctx(sel_rfd *rfd);
void sel_rfd_set_ctx(sel_rfd *rfd, void *ctx);

/*
 * Run one iteration of the sel event loop, calling callbacks as
 * necessary. Returns zero on success; in the event of a fatal
 * error, returns the errno value.
 * 
 * "timeout" is a value in microseconds to limit the length of the
 * select call. Less than zero means to wait indefinitely.
 */
int sel_iterate(sel *sel, long timeout);

/*
 * Change the underlying fd in a wfd. If set to -1, no write
 * attempts will take place and the wfd's buffer will simply store
 * everything passed to sel_write(). If later set to something
 * other than -1, all that buffered data will become eligible for
 * real writing.
 */
void sel_wfd_setfd(sel_wfd *wfd, int fd);

/*
 * Change the underlying fd in a rfd. If set to -1, no read
 * attempts will take place.
 */
void sel_rfd_setfd(sel_rfd *rfd, int fd);

#endif /* FIXME_SEL_H */
