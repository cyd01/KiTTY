/******************************************************************************
 *									      *
 *				   N O T I C E				      *
 *									      *
 *		      Copyright Abandoned, 1987, Fred Fish		      *
 *									      *
 *									      *
 *	This previously copyrighted work has been placed into the  public     *
 *	domain	by  the  author  and  may be freely used for any purpose,     *
 *	private or commercial.						      *
 *									      *
 *	Because of the number of inquiries I was receiving about the  use     *
 *	of this product in commercially developed works I have decided to     *
 *	simply make it public domain to further its unrestricted use.	I     *
 *	specifically  would  be  most happy to see this material become a     *
 *	part of the standard Unix distributions by AT&T and the  Berkeley     *
 *	Computer  Science  Research Group, and a standard part of the GNU     *
 *	system from the Free Software Foundation.			      *
 *									      *
 *	I would appreciate it, as a courtesy, if this notice is  left  in     *
 *	all copies and derivative works.  Thank you.			      *
 *									      *
 *	The author makes no warranty of any kind  with	respect  to  this     *
 *	product  and  explicitly disclaims any implied warranties of mer-     *
 *	chantability or fitness for any particular purpose.		      *
 *									      *
 ******************************************************************************
 */


/*
 *  FILE
 *
 *	dbug.c	 runtime support routines for dbug package
 *
 *  SCCS
 *
 *	@(#)dbug.c	1.25	7/25/89
 *
 *  DESCRIPTION
 *
 *	These are the runtime support routines for the dbug package.
 *	The dbug package has two main components; the user include
 *	file containing various macro definitions, and the runtime
 *	support routines which are called from the macro expansions.
 *
 *	Externally visible functions in the runtime support module
 *	use the naming convention pattern "_dbug_xx...xx_", thus
 *	they are unlikely to collide with user defined function names.
 *
 *  AUTHOR(S)
 *
 *	Fred Fish		(base code)
 *	Enhanced Software Technologies, Tempe, AZ
 *	asuvax!mcdphx!estinc!fnf
 *
 *	Binayak Banerjee	(profiling enhancements)
 *	seismo!bpa!sjuvax!bbanerje
 *
 *	Michael Widenius:
 *	DBUG_DUMP	- To dump a pice of memory.
 *	PUSH_FLAG "O"	- To be used insted of "o" if we don't
 *			  want flushing (for slow systems)
 *	PUSH_FLAG "A"	- as 'O', but we will append to the out file instead
 *			  of creating a new one.
 *	Check of malloc on entry/exit (option "S")
 */

#ifndef dbug_h
#define dbug_h

#include <stdio.h>
#include <stdlib.h>

#ifdef	__cplusplus
extern "C" {
#endif

  extern char dbug_dig_vec[];

#if !defined(DBUG_OFF) && !defined(_lint)
  extern int dbug_on, dbug_none;
  extern FILE *dbug_fp;
  extern const char *dbug_process;
  extern int dbug_keyword(const char *keyword);
  extern void dbug_setjmp(void);
  extern void dbug_longjmp(void);
  extern void dbug_push(const char *control);
  extern void dbug_pop(void);
  extern void dbug_enter(const char *func, const char *file,
			 unsigned line, const char **sfunc,
			 const char **sfile, unsigned * slevel, char ***);
  extern void dbug_thread_enter(const char *func, const char *file,
				unsigned line, const char **sfunc,
				const char **sfile, unsigned *slevel,
				char ***);

  extern void dbug_return(unsigned line, const char **sfunc,
			     const char **sfile, unsigned *slevel);
  extern void dbug_pargs(unsigned line, const char *keyword);
  extern void dbug_doprnt(const char *format, ...);
  extern void dbug_dump(unsigned line, const char *keyword,
			   const char *memory, unsigned length);

#define DBUG_INIT(process,name,opts) DBUG_frame(name,DBUG_PROCESS(process);DBUG_PUSH(opts);)
#define DBUG_INIT_ENV(process,name,optsvar) DBUG_frame(name,DBUG_PROCESS(process);DBUG_PUSH_ENV(optsvar);)
#define DBUG_ENTERF() DBUG_ENTER(__func__)
#define DBUG_ENTER(name) DBUG_frame(name,(void))
#define DBUG_frame(name,init) \
	const char *dbug_func, *dbug_file; unsigned dbug_level; \
	char **dbug_framep; \
	init \
	dbug_enter(name,__FILE__,__LINE__,&dbug_func,&dbug_file,&dbug_level, \
		   &dbug_framep)
#define DBUG_LEAVE dbug_return(__LINE__, &dbug_func, &dbug_file, &dbug_level)
#define DBUG_RETURN(a1) do{ DBUG_LEAVE; return(a1); }while(0)
#define DBUG_VOID_RETURN do{ DBUG_LEAVE; return; }while(0)
#define DBUG_EXECUTE(keyword,stmt) \
	do{ if (dbug_on && dbug_keyword(keyword)) { stmt } }while(0)
#define DBUG_PRINT(keyword,arglist) \
	do{ if (dbug_on) { dbug_pargs(__LINE__,keyword); dbug_doprnt arglist; }}while(0)
#define DBUG_PUSH_ENV(optsvar) \
	do{ char *opts; if ((opts = getenv(optsvar))) dbug_push(opts); }while(0)
#define DBUG_PUSH(opts) dbug_push(opts)
#define DBUG_POP() dbug_pop()
#define DBUG_PROCESS(a1) (dbug_process = a1)
#define DBUG_FILE (dbug_fp)
#define DBUG_SETJMP(a1) (dbug_setjmp(), setjmp(a1))
#define DBUG_LONGJMP(a1,a2) (dbug_longjmp(), longjmp(a1, a2))
#define DBUG_DUMP(keyword,a1,a2)\
	do{ if (dbug_on) { dbug_dump(__LINE__,keyword,a1,a2); } }while(0)
#define DBUG_IN_USE (dbug_fp && dbug_fp != stderr)
#define DEBUGGER_OFF (dbug_none = !(dbug_on = 0))
#define DEBUGGER_ON (dbug_none = 0)
#define DBUG_ASSERT(A) assert(A)
#else				/* No debugger */

#define DBUG_INIT(process,name,opts)
#define DBUG_INIT_ENV(process,name,optsvar)
#define DBUG_ENTERF()
#define DBUG_ENTER(a1)
#define DBUG_LEAVE
#define DBUG_RETURN(a1) return(a1)
#define DBUG_VOID_RETURN return
#define DBUG_EXECUTE(keyword,a1)
#define DBUG_PRINT(keyword,arglist)
#define DBUG_PUSH_ENV(a1)
#define DBUG_PUSH(a1)
#define DBUG_POP()
#define DBUG_PROCESS(a1)
#define DBUG_FILE (stderr)
#define DBUG_SETJMP setjmp
#define DBUG_LONGJMP longjmp
#define DBUG_DUMP(keyword,a1,a2)
#define DBUG_IN_USE 0
#define DEBUGGER_OFF
#define DEBUGGER_ON
#define DBUG_ASSERT(A)
#endif
#ifdef	__cplusplus
}
#endif
#endif /* dbug_h */
