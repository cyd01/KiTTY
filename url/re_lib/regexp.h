/*
 * Definitions etc. for regexp(3) routines.
 *
 * Caveat:  this is V8 regexp(3) [actually, a reimplementation thereof],
 * not the System V one.
 */
#define NSUBEXP  16
typedef struct regexp {
	char *startp[NSUBEXP];
	char *endp[NSUBEXP];
	char regstart;		/* Internal use only. */
	char reganch;		/* Internal use only. */
	char *regmust;		/* Internal use only. */
	int regmlen;		/* Internal use only. */
	char program[1];	/* Unwarranted chumminess with compiler. */
} regexp;

regexp *regcomp( char* re);
int regexec( regexp* r, char* str);
void regsub( regexp* r, char* str, char* substr);
void regerror( char* s);	/* for internal use only */
void set_regerror_func( void (*func)( char*));

void regfree( regexp* r) ;