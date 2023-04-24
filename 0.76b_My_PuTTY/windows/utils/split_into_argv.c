/*
 * Split a complete command line into argc/argv, attempting to do it
 * exactly the same way the Visual Studio C library would do it (so
 * that our console utilities, which receive argc and argv already
 * broken apart by the C library, will have their command lines
 * processed in the same way as the GUI utilities which get a whole
 * command line and must call this function).
 *
 * Does not modify the input command line.
 *
 * The final parameter (argstart) is used to return a second array
 * of char * pointers, the same length as argv, each one pointing
 * at the start of the corresponding element of argv in the
 * original command line. So if you get half way through processing
 * your command line in argc/argv form and then decide you want to
 * treat the rest as a raw string, you can. If you don't want to,
 * `argstart' can be safely left NULL.
 */

#include "putty.h"

/*
 * The precise argument-breaking rules vary with compiler version, or
 * rather, with the crt0-type startup code that comes with each
 * compiler's C library. We do our best to match the compiler version,
 * so that we faithfully imitate in our GUI utilities what the
 * corresponding set of CLI utilities can't be prevented from doing.
 *
 * The basic rules are:
 *
 *  - Single quotes are not special characters.
 *
 *  - Double quotes are removed, but within them spaces cease to be
 *    special.
 *
 *  - Backslashes are _only_ special when a sequence of them appear
 *    just before a double quote. In this situation, they are treated
 *    like C backslashes: so \" just gives a literal quote, \\" gives
 *    a literal backslash and then opens or closes a double-quoted
 *    segment, \\\" gives a literal backslash and then a literal
 *    quote, \\\\" gives two literal backslashes and then opens/closes
 *    a double-quoted segment, and so forth. Note that this behaviour
 *    is identical inside and outside double quotes.
 *
 *  - Two successive double quotes become one literal double quote,
 *    but only _inside_ a double-quoted segment. Outside, they just
 *    form an empty double-quoted segment (which may cause an empty
 *    argument word).
 *
 * That only leaves the interesting question of what happens when one
 * or more backslashes precedes two or more double quotes, starting
 * inside a double-quoted string.
 *
 * Modern Visual Studio (as of 2021)
 * ---------------------------------
 *
 * I investigated this in an ordinary CLI program, using the
 * toolchain's crt0 to split a command line of the form
 *
 *    "a\\\"""b c" d
 *
 * Here I tabulate number of backslashes (across the top) against
 * number of quotes (down the left), and indicate how many backslashes
 * are output, how many quotes are output, and whether a quoted
 * segment is open at the end of the sequence:
 *
 *                      backslashes
 *
 *               0         1      2      3      4
 *
 *         0   0,0,y  |  1,0,y  2,0,y  3,0,y  4,0,y
 *            --------+-----------------------------
 *         1   0,0,n  |  0,1,y  1,0,n  1,1,y  2,0,n
 *    q    2   0,1,y  |  0,1,n  1,1,y  1,1,n  2,1,y
 *    u    3   0,1,n  |  0,2,y  1,1,n  1,2,y  2,1,n
 *    o    4   0,2,y  |  0,2,n  1,2,y  1,2,n  2,2,y
 *    t    5   0,2,n  |  0,3,y  1,2,n  1,3,y  2,2,n
 *    e    6   0,3,y  |  0,3,n  1,3,y  1,3,n  2,3,y
 *    s    7   0,3,n  |  0,4,y  1,3,n  1,4,y  2,3,n
 *         8   0,4,y  |  0,4,n  1,4,y  1,4,n  2,4,y
 *
 * The row at the top of this table, with quotes=0, demonstrates what
 * I claimed above, that when a sequence of backslashes are not
 * followed by a double quote, they don't act specially at all. The
 * rest of the table shows that the backslashes escape each other in
 * pairs (so that with 2n or 2n+1 input backslashes you get n output
 * ones); if there's an odd number of input backslashes then the last
 * one escapes the first double quote (so you get a literal quote and
 * enter a quoted string); thereafter, each input quote character
 * either opens or closes a quoted string, and if it closes one, it
 * generates a literal " as a side effect.
 *
 * Older Visual Studio
 * -------------------
 *
 * But here's the corresponding table from the older Visual Studio 7:
 *
 *                      backslashes
 *
 *               0         1      2      3      4
 *
 *         0   0,0,y  |  1,0,y  2,0,y  3,0,y  4,0,y
 *            --------+-----------------------------
 *         1   0,0,n  |  0,1,y  1,0,n  1,1,y  2,0,n
 *    q    2   0,1,n  |  0,1,n  1,1,n  1,1,n  2,1,n
 *    u    3   0,1,y  |  0,2,n  1,1,y  1,2,n  2,1,y
 *    o    4   0,1,n  |  0,2,y  1,1,n  1,2,y  2,1,n
 *    t    5   0,2,n  |  0,2,n  1,2,n  1,2,n  2,2,n
 *    e    6   0,2,y  |  0,3,n  1,2,y  1,3,n  2,2,y
 *    s    7   0,2,n  |  0,3,y  1,2,n  1,3,y  2,2,n
 *         8   0,3,n  |  0,3,n  1,3,n  1,3,n  2,3,n
 *         9   0,3,y  |  0,4,n  1,3,y  1,4,n  2,3,y
 *        10   0,3,n  |  0,4,y  1,3,n  1,4,y  2,3,n
 *        11   0,4,n  |  0,4,n  1,4,n  1,4,n  2,4,n
 *
 * There is very weird mod-3 behaviour going on here in the number of
 * quotes, and it even applies when there aren't any backslashes! How
 * ghastly.
 *
 * With a bit of thought, this extremely odd diagram suddenly
 * coalesced itself into a coherent, if still ghastly, model of how
 * things work:
 *
 *  - As before, backslashes are only special when one or more of them
 *    appear contiguously before at least one double quote. In this
 *    situation the backslashes do exactly what you'd expect: each one
 *    quotes the next thing in front of it, so you end up with n/2
 *    literal backslashes (if n is even) or (n-1)/2 literal
 *    backslashes and a literal quote (if n is odd). In the latter
 *    case the double quote character right after the backslashes is
 *    used up.
 *
 *  - After that, any remaining double quotes are processed. A string
 *    of contiguous unescaped double quotes has a mod-3 behaviour:
 *
 *     * inside a quoted segment, a quote ends the segment.
 *     * _immediately_ after ending a quoted segment, a quote simply
 *       produces a literal quote.
 *     * otherwise, outside a quoted segment, a quote begins a quoted
 *       segment.
 *
 *    So, for example, if we started inside a quoted segment then two
 *    contiguous quotes would close the segment and produce a literal
 *    quote; three would close the segment, produce a literal quote,
 *    and open a new segment. If we started outside a quoted segment,
 *    then two contiguous quotes would open and then close a segment,
 *    producing no output (but potentially creating a zero-length
 *    argument); but three quotes would open and close a segment and
 *    then produce a literal quote.
 *
 * I don't know exactly when the bug fix happened, but I know that VS7
 * had the odd mod-3 behaviour. So the #if below will ensure that
 * modern (2015 onwards) versions of VS use the new more sensible
 * behaviour, and VS7 uses the old one. Things in between may be
 * wrong; if anyone cares, patches to change the cutoff version in
 * this #if are welcome.
 */
#if _MSC_VER < 1400
#define MOD3 1
#else
#define MOD3 0
#endif

void split_into_argv(char *cmdline, int *argc, char ***argv,
                     char ***argstart)
{
    char *p;
    char *outputline, *q;
    char **outputargv, **outputargstart;
    int outputargc;

    /*
     * First deal with the simplest of all special cases: if there
     * aren't any arguments, return 0,NULL,NULL.
     */
    while (*cmdline && isspace(*cmdline)) cmdline++;
    if (!*cmdline) {
        if (argc) *argc = 0;
        if (argv) *argv = NULL;
        if (argstart) *argstart = NULL;
        return;
    }

    /*
     * This will guaranteeably be big enough; we can realloc it
     * down later.
     */
    outputline = snewn(1+strlen(cmdline), char);
    outputargv = snewn(strlen(cmdline)+1 / 2, char *);
    outputargstart = snewn(strlen(cmdline)+1 / 2, char *);

    p = cmdline; q = outputline; outputargc = 0;

    while (*p) {
        bool quote;

        /* Skip whitespace searching for start of argument. */
        while (*p && isspace(*p)) p++;
        if (!*p) break;

        /* We have an argument; start it. */
        outputargv[outputargc] = q;
        outputargstart[outputargc] = p;
        outputargc++;
        quote = false;

        /* Copy data into the argument until it's finished. */
        while (*p) {
            if (!quote && isspace(*p))
                break;                 /* argument is finished */

            if (*p == '"' || *p == '\\') {
                /*
                 * We have a sequence of zero or more backslashes
                 * followed by a sequence of zero or more quotes.
                 * Count up how many of each, and then deal with
                 * them as appropriate.
                 */
                int i, slashes = 0, quotes = 0;
                while (*p == '\\') slashes++, p++;
                while (*p == '"') quotes++, p++;

                if (!quotes) {
                    /*
                     * Special case: if there are no quotes,
                     * slashes are not special at all, so just copy
                     * n slashes to the output string.
                     */
                    while (slashes--) *q++ = '\\';
                } else {
                    /* Slashes annihilate in pairs. */
                    while (slashes >= 2) slashes -= 2, *q++ = '\\';

                    /* One remaining slash takes out the first quote. */
                    if (slashes) quotes--, *q++ = '"';

                    if (quotes > 0) {
                        /* Outside a quote segment, a quote starts one. */
                        if (!quote) quotes--;

#if !MOD3
                        /* New behaviour: produce n/2 literal quotes... */
                        for (i = 2; i <= quotes; i += 2) *q++ = '"';
                        /* ... and end in a quote segment iff 2 divides n. */
                        quote = (quotes % 2 == 0);
#else
                        /* Old behaviour: produce (n+1)/3 literal quotes... */
                        for (i = 3; i <= quotes+1; i += 3) *q++ = '"';
                        /* ... and end in a quote segment iff 3 divides n. */
                        quote = (quotes % 3 == 0);
#endif
                    }
                }
            } else {
                *q++ = *p++;
            }
        }

        /* At the end of an argument, just append a trailing NUL. */
        *q++ = '\0';
    }

    outputargv = sresize(outputargv, outputargc, char *);
    outputargstart = sresize(outputargstart, outputargc, char *);

    if (argc) *argc = outputargc;
    if (argv) *argv = outputargv; else sfree(outputargv);
    if (argstart) *argstart = outputargstart; else sfree(outputargstart);
}

#ifdef TEST

const struct argv_test {
    const char *cmdline;
    const char *argv[10];
} argv_tests[] = {
    /*
     * We generate this set of tests by invoking ourself with
     * `-generate'.
     */
#if !MOD3
    /* Newer behaviour, with no weird mod-3 glitch. */
    {"ab c\" d", {"ab", "c d", NULL}},
    {"a\"b c\" d", {"ab c", "d", NULL}},
    {"a\"\"b c\" d", {"ab", "c d", NULL}},
    {"a\"\"\"b c\" d", {"a\"b c", "d", NULL}},
    {"a\"\"\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"a\"\"\"\"\"b c\" d", {"a\"\"b c", "d", NULL}},
    {"a\"\"\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"a\"\"\"\"\"\"\"b c\" d", {"a\"\"\"b c", "d", NULL}},
    {"a\"\"\"\"\"\"\"\"b c\" d", {"a\"\"\"b", "c d", NULL}},
    {"a\\b c\" d", {"a\\b", "c d", NULL}},
    {"a\\\"b c\" d", {"a\"b", "c d", NULL}},
    {"a\\\"\"b c\" d", {"a\"b c", "d", NULL}},
    {"a\\\"\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"a\\\"\"\"\"b c\" d", {"a\"\"b c", "d", NULL}},
    {"a\\\"\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"a\\\"\"\"\"\"\"b c\" d", {"a\"\"\"b c", "d", NULL}},
    {"a\\\"\"\"\"\"\"\"b c\" d", {"a\"\"\"b", "c d", NULL}},
    {"a\\\"\"\"\"\"\"\"\"b c\" d", {"a\"\"\"\"b c", "d", NULL}},
    {"a\\\\b c\" d", {"a\\\\b", "c d", NULL}},
    {"a\\\\\"b c\" d", {"a\\b c", "d", NULL}},
    {"a\\\\\"\"b c\" d", {"a\\b", "c d", NULL}},
    {"a\\\\\"\"\"b c\" d", {"a\\\"b c", "d", NULL}},
    {"a\\\\\"\"\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"a\\\\\"\"\"\"\"b c\" d", {"a\\\"\"b c", "d", NULL}},
    {"a\\\\\"\"\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"a\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b c", "d", NULL}},
    {"a\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b", "c d", NULL}},
    {"a\\\\\\b c\" d", {"a\\\\\\b", "c d", NULL}},
    {"a\\\\\\\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"a\\\\\\\"\"b c\" d", {"a\\\"b c", "d", NULL}},
    {"a\\\\\\\"\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"a\\\\\\\"\"\"\"b c\" d", {"a\\\"\"b c", "d", NULL}},
    {"a\\\\\\\"\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"a\\\\\\\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b c", "d", NULL}},
    {"a\\\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b", "c d", NULL}},
    {"a\\\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"\"b c", "d", NULL}},
    {"a\\\\\\\\b c\" d", {"a\\\\\\\\b", "c d", NULL}},
    {"a\\\\\\\\\"b c\" d", {"a\\\\b c", "d", NULL}},
    {"a\\\\\\\\\"\"b c\" d", {"a\\\\b", "c d", NULL}},
    {"a\\\\\\\\\"\"\"b c\" d", {"a\\\\\"b c", "d", NULL}},
    {"a\\\\\\\\\"\"\"\"b c\" d", {"a\\\\\"b", "c d", NULL}},
    {"a\\\\\\\\\"\"\"\"\"b c\" d", {"a\\\\\"\"b c", "d", NULL}},
    {"a\\\\\\\\\"\"\"\"\"\"b c\" d", {"a\\\\\"\"b", "c d", NULL}},
    {"a\\\\\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\\\"\"\"b c", "d", NULL}},
    {"a\\\\\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\\\"\"\"b", "c d", NULL}},
    {"\"ab c\" d", {"ab c", "d", NULL}},
    {"\"a\"b c\" d", {"ab", "c d", NULL}},
    {"\"a\"\"b c\" d", {"a\"b c", "d", NULL}},
    {"\"a\"\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"\"a\"\"\"\"b c\" d", {"a\"\"b c", "d", NULL}},
    {"\"a\"\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"\"a\"\"\"\"\"\"b c\" d", {"a\"\"\"b c", "d", NULL}},
    {"\"a\"\"\"\"\"\"\"b c\" d", {"a\"\"\"b", "c d", NULL}},
    {"\"a\"\"\"\"\"\"\"\"b c\" d", {"a\"\"\"\"b c", "d", NULL}},
    {"\"a\\b c\" d", {"a\\b c", "d", NULL}},
    {"\"a\\\"b c\" d", {"a\"b c", "d", NULL}},
    {"\"a\\\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"\"a\\\"\"\"b c\" d", {"a\"\"b c", "d", NULL}},
    {"\"a\\\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"\"a\\\"\"\"\"\"b c\" d", {"a\"\"\"b c", "d", NULL}},
    {"\"a\\\"\"\"\"\"\"b c\" d", {"a\"\"\"b", "c d", NULL}},
    {"\"a\\\"\"\"\"\"\"\"b c\" d", {"a\"\"\"\"b c", "d", NULL}},
    {"\"a\\\"\"\"\"\"\"\"\"b c\" d", {"a\"\"\"\"b", "c d", NULL}},
    {"\"a\\\\b c\" d", {"a\\\\b c", "d", NULL}},
    {"\"a\\\\\"b c\" d", {"a\\b", "c d", NULL}},
    {"\"a\\\\\"\"b c\" d", {"a\\\"b c", "d", NULL}},
    {"\"a\\\\\"\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"\"a\\\\\"\"\"\"b c\" d", {"a\\\"\"b c", "d", NULL}},
    {"\"a\\\\\"\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"\"a\\\\\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b c", "d", NULL}},
    {"\"a\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b", "c d", NULL}},
    {"\"a\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"\"b c", "d", NULL}},
    {"\"a\\\\\\b c\" d", {"a\\\\\\b c", "d", NULL}},
    {"\"a\\\\\\\"b c\" d", {"a\\\"b c", "d", NULL}},
    {"\"a\\\\\\\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"\"a\\\\\\\"\"\"b c\" d", {"a\\\"\"b c", "d", NULL}},
    {"\"a\\\\\\\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"\"a\\\\\\\"\"\"\"\"b c\" d", {"a\\\"\"\"b c", "d", NULL}},
    {"\"a\\\\\\\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b", "c d", NULL}},
    {"\"a\\\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"\"b c", "d", NULL}},
    {"\"a\\\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"\"b", "c d", NULL}},
    {"\"a\\\\\\\\b c\" d", {"a\\\\\\\\b c", "d", NULL}},
    {"\"a\\\\\\\\\"b c\" d", {"a\\\\b", "c d", NULL}},
    {"\"a\\\\\\\\\"\"b c\" d", {"a\\\\\"b c", "d", NULL}},
    {"\"a\\\\\\\\\"\"\"b c\" d", {"a\\\\\"b", "c d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"b c\" d", {"a\\\\\"\"b c", "d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"\"b c\" d", {"a\\\\\"\"b", "c d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"\"\"b c\" d", {"a\\\\\"\"\"b c", "d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\\\"\"\"b", "c d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\\\"\"\"\"b c", "d", NULL}},
#else /* MOD3 */
    /* VS7 mod-3 behaviour. */
    {"ab c\" d", {"ab", "c d", NULL}},
    {"a\"b c\" d", {"ab c", "d", NULL}},
    {"a\"\"b c\" d", {"ab", "c d", NULL}},
    {"a\"\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"a\"\"\"\"b c\" d", {"a\"b c", "d", NULL}},
    {"a\"\"\"\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"a\"\"\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"a\"\"\"\"\"\"\"b c\" d", {"a\"\"b c", "d", NULL}},
    {"a\"\"\"\"\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"a\\b c\" d", {"a\\b", "c d", NULL}},
    {"a\\\"b c\" d", {"a\"b", "c d", NULL}},
    {"a\\\"\"b c\" d", {"a\"b c", "d", NULL}},
    {"a\\\"\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"a\\\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"a\\\"\"\"\"\"b c\" d", {"a\"\"b c", "d", NULL}},
    {"a\\\"\"\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"a\\\"\"\"\"\"\"\"b c\" d", {"a\"\"\"b", "c d", NULL}},
    {"a\\\"\"\"\"\"\"\"\"b c\" d", {"a\"\"\"b c", "d", NULL}},
    {"a\\\\b c\" d", {"a\\\\b", "c d", NULL}},
    {"a\\\\\"b c\" d", {"a\\b c", "d", NULL}},
    {"a\\\\\"\"b c\" d", {"a\\b", "c d", NULL}},
    {"a\\\\\"\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"a\\\\\"\"\"\"b c\" d", {"a\\\"b c", "d", NULL}},
    {"a\\\\\"\"\"\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"a\\\\\"\"\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"a\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\"\"b c", "d", NULL}},
    {"a\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"a\\\\\\b c\" d", {"a\\\\\\b", "c d", NULL}},
    {"a\\\\\\\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"a\\\\\\\"\"b c\" d", {"a\\\"b c", "d", NULL}},
    {"a\\\\\\\"\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"a\\\\\\\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"a\\\\\\\"\"\"\"\"b c\" d", {"a\\\"\"b c", "d", NULL}},
    {"a\\\\\\\"\"\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"a\\\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b", "c d", NULL}},
    {"a\\\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b c", "d", NULL}},
    {"a\\\\\\\\b c\" d", {"a\\\\\\\\b", "c d", NULL}},
    {"a\\\\\\\\\"b c\" d", {"a\\\\b c", "d", NULL}},
    {"a\\\\\\\\\"\"b c\" d", {"a\\\\b", "c d", NULL}},
    {"a\\\\\\\\\"\"\"b c\" d", {"a\\\\\"b", "c d", NULL}},
    {"a\\\\\\\\\"\"\"\"b c\" d", {"a\\\\\"b c", "d", NULL}},
    {"a\\\\\\\\\"\"\"\"\"b c\" d", {"a\\\\\"b", "c d", NULL}},
    {"a\\\\\\\\\"\"\"\"\"\"b c\" d", {"a\\\\\"\"b", "c d", NULL}},
    {"a\\\\\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\\\"\"b c", "d", NULL}},
    {"a\\\\\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\\\"\"b", "c d", NULL}},
    {"\"ab c\" d", {"ab c", "d", NULL}},
    {"\"a\"b c\" d", {"ab", "c d", NULL}},
    {"\"a\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"\"a\"\"\"b c\" d", {"a\"b c", "d", NULL}},
    {"\"a\"\"\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"\"a\"\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"\"a\"\"\"\"\"\"b c\" d", {"a\"\"b c", "d", NULL}},
    {"\"a\"\"\"\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"\"a\"\"\"\"\"\"\"\"b c\" d", {"a\"\"\"b", "c d", NULL}},
    {"\"a\\b c\" d", {"a\\b c", "d", NULL}},
    {"\"a\\\"b c\" d", {"a\"b c", "d", NULL}},
    {"\"a\\\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"\"a\\\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"\"a\\\"\"\"\"b c\" d", {"a\"\"b c", "d", NULL}},
    {"\"a\\\"\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"\"a\\\"\"\"\"\"\"b c\" d", {"a\"\"\"b", "c d", NULL}},
    {"\"a\\\"\"\"\"\"\"\"b c\" d", {"a\"\"\"b c", "d", NULL}},
    {"\"a\\\"\"\"\"\"\"\"\"b c\" d", {"a\"\"\"b", "c d", NULL}},
    {"\"a\\\\b c\" d", {"a\\\\b c", "d", NULL}},
    {"\"a\\\\\"b c\" d", {"a\\b", "c d", NULL}},
    {"\"a\\\\\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"\"a\\\\\"\"\"b c\" d", {"a\\\"b c", "d", NULL}},
    {"\"a\\\\\"\"\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"\"a\\\\\"\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"\"a\\\\\"\"\"\"\"\"b c\" d", {"a\\\"\"b c", "d", NULL}},
    {"\"a\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"\"a\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b", "c d", NULL}},
    {"\"a\\\\\\b c\" d", {"a\\\\\\b c", "d", NULL}},
    {"\"a\\\\\\\"b c\" d", {"a\\\"b c", "d", NULL}},
    {"\"a\\\\\\\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"\"a\\\\\\\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"\"a\\\\\\\"\"\"\"b c\" d", {"a\\\"\"b c", "d", NULL}},
    {"\"a\\\\\\\"\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"\"a\\\\\\\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b", "c d", NULL}},
    {"\"a\\\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b c", "d", NULL}},
    {"\"a\\\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b", "c d", NULL}},
    {"\"a\\\\\\\\b c\" d", {"a\\\\\\\\b c", "d", NULL}},
    {"\"a\\\\\\\\\"b c\" d", {"a\\\\b", "c d", NULL}},
    {"\"a\\\\\\\\\"\"b c\" d", {"a\\\\\"b", "c d", NULL}},
    {"\"a\\\\\\\\\"\"\"b c\" d", {"a\\\\\"b c", "d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"b c\" d", {"a\\\\\"b", "c d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"\"b c\" d", {"a\\\\\"\"b", "c d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"\"\"b c\" d", {"a\\\\\"\"b c", "d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\\\"\"b", "c d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\\\"\"\"b", "c d", NULL}},
#endif /* MOD3 */
};

void out_of_memory(void)
{
    fprintf(stderr, "out of memory!\n");
    exit(2);
}

int main(int argc, char **argv)
{
    int i, j;

    if (argc > 1) {
        /*
         * Generation of tests.
         *
         * Given `-splat <args>', we print out a C-style
         * representation of each argument (in the form "a", "b",
         * NULL), backslash-escaping each backslash and double
         * quote.
         *
         * Given `-split <string>', we first doctor `string' by
         * turning forward slashes into backslashes, single quotes
         * into double quotes and underscores into spaces; and then
         * we feed the resulting string to ourself with `-splat'.
         *
         * Given `-generate', we concoct a variety of fun test
         * cases, encode them in quote-safe form (mapping \, " and
         * space to /, ' and _ respectively) and feed each one to
         * `-split'.
         */
        if (!strcmp(argv[1], "-splat")) {
            int i;
            char *p;
            for (i = 2; i < argc; i++) {
                putchar('"');
                for (p = argv[i]; *p; p++) {
                    if (*p == '\\' || *p == '"')
                        putchar('\\');
                    putchar(*p);
                }
                printf("\", ");
            }
            printf("NULL");
            return 0;
        }

        if (!strcmp(argv[1], "-split") && argc > 2) {
            strbuf *cmdline = strbuf_new();
            char *p;

            put_fmt(cmdline, "%s -splat ", argv[0]);
            printf("    {\"");
            size_t args_start = cmdline->len;
            for (p = argv[2]; *p; p++) {
                char c = (*p == '/' ? '\\' :
                          *p == '\'' ? '"' :
                          *p == '_' ? ' ' :
                          *p);
                put_byte(cmdline, c);
            }
            write_c_string_literal(stdout, ptrlen_from_asciz(
                                       cmdline->s + args_start));
            printf("\", {");
            fflush(stdout);

            system(cmdline->s);

            printf("}},\n");

            strbuf_free(cmdline);
            return 0;
        }

        if (!strcmp(argv[1], "-generate")) {
            char *teststr, *p;
            int i, initialquote, backslashes, quotes;

            teststr = malloc(200 + strlen(argv[0]));

            for (initialquote = 0; initialquote <= 1; initialquote++) {
                for (backslashes = 0; backslashes < 5; backslashes++) {
                    for (quotes = 0; quotes < 9; quotes++) {
                        p = teststr + sprintf(teststr, "%s -split ", argv[0]);
                        if (initialquote) *p++ = '\'';
                        *p++ = 'a';
                        for (i = 0; i < backslashes; i++) *p++ = '/';
                        for (i = 0; i < quotes; i++) *p++ = '\'';
                        *p++ = 'b';
                        *p++ = '_';
                        *p++ = 'c';
                        *p++ = '\'';
                        *p++ = '_';
                        *p++ = 'd';
                        *p = '\0';

                        system(teststr);
                    }
                }
            }
            return 0;
        }

        if (!strcmp(argv[1], "-tabulate")) {
            char table[] = "\
 *                      backslashes                   \n\
 *                                                    \n\
 *               0         1      2      3      4     \n\
 *                                                    \n\
 *         0          |                               \n\
 *            --------+-----------------------------  \n\
 *         1          |                               \n\
 *    q    2          |                               \n\
 *    u    3          |                               \n\
 *    o    4          |                               \n\
 *    t    5          |                               \n\
 *    e    6          |                               \n\
 *    s    7          |                               \n\
 *         8          |                               \n\
";
            char *linestarts[14];
            char *p = table;
            for (i = 0; i < lenof(linestarts); i++) {
                linestarts[i] = p;
                p += strcspn(p, "\n");
                if (*p) p++;
            }

            for (i = 0; i < lenof(argv_tests); i++) {
                const struct argv_test *test = &argv_tests[i];
                const char *q = test->cmdline;

                /* Skip tests that aren't telling us something about
                 * the behaviour _inside_ a quoted string */
                if (*q != '"')
                    continue;

                q++;

                assert(*q == 'a');
                q++;
                int backslashes_in = 0, quotes_in = 0;
                while (*q == '\\') {
                    q++;
                    backslashes_in++;
                }
                while (*q == '"') {
                    q++;
                    quotes_in++;
                }

                q = test->argv[0];
                assert(*q == 'a');
                q++;
                int backslashes_out = 0, quotes_out = 0;
                while (*q == '\\') {
                    q++;
                    backslashes_out++;
                }
                while (*q == '"') {
                    q++;
                    quotes_out++;
                }
                assert(*q == 'b');
                q++;
                bool in_quoted_string = (*q == ' ');

                int x = (backslashes_in == 0 ? 15 : 18 + 7 * backslashes_in);
                int y = (quotes_in == 0 ? 4 : 5 + quotes_in);
                char *buf = dupprintf("%d,%d,%c",
                                      backslashes_out, quotes_out,
                                      in_quoted_string ? 'y' : 'n');
                memcpy(linestarts[y] + x, buf, strlen(buf));
                sfree(buf);
            }

            fputs(table, stdout);
            return 0;
        }

        fprintf(stderr, "unrecognised option: \"%s\"\n", argv[1]);
        return 1;
    }

    /*
     * If we get here, we were invoked with no arguments, so just
     * run the tests.
     */
    int passes = 0, fails = 0;

    for (i = 0; i < lenof(argv_tests); i++) {
        int ac;
        char **av;
        bool failed = false;

        split_into_argv((char *)argv_tests[i].cmdline, &ac, &av, NULL);

        for (j = 0; j < ac && argv_tests[i].argv[j]; j++) {
            if (strcmp(av[j], argv_tests[i].argv[j])) {
                printf("failed test %d (|%s|) arg %d: |%s| should be |%s|\n",
                       i, argv_tests[i].cmdline,
                       j, av[j], argv_tests[i].argv[j]);
                failed = true;
            }
#ifdef VERBOSE
            else {
                printf("test %d (|%s|) arg %d: |%s| == |%s|\n",
                       i, argv_tests[i].cmdline,
                       j, av[j], argv_tests[i].argv[j]);
            }
#endif
        }
        if (j < ac) {
            printf("failed test %d (|%s|): %d args returned, should be %d\n",
                   i, argv_tests[i].cmdline, ac, j);
            failed = true;
        }
        if (argv_tests[i].argv[j]) {
            printf("failed test %d (|%s|): %d args returned, should be more\n",
                   i, argv_tests[i].cmdline, ac);
            failed = true;
        }

        if (failed)
            fails++;
        else
            passes++;
    }

    printf("passed %d failed %d (%s mode)\n",
           passes, fails,
#if MOD3
           "mod 3"
#else
           "mod 2"
#endif
        );

    return fails != 0;
}

#endif /* TEST */
