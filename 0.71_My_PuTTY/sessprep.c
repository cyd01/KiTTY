/*
 * sessprep.c: centralise some preprocessing done on Conf objects
 * before launching them.
 */

#include "putty.h"

void prepare_session(Conf *conf)
{
    char *hostbuf = dupstr(conf_get_str(conf, CONF_host));
    char *host = hostbuf;
    char *p, *q;

    /*
     * Trim leading whitespace from the hostname.
     */
    host += strspn(host, " \t");

    /*
     * See if host is of the form user@host, and separate out the
     * username if so.
     */
    if (host[0] != '\0') {
        /*
         * Use strrchr, in case the _username_ in turn is of the form
         * user@host, which has been known.
         */
        char *atsign = strrchr(host, '@');
        if (atsign) {
            *atsign = '\0';
            conf_set_str(conf, CONF_username, host);
            host = atsign + 1;
        }
    }

    /*
     * Trim a colon suffix off the hostname if it's there, and discard
     * the text after it.
     *
     * The exact reason why we _ignore_ this text, rather than
     * treating it as a port number, is unfortunately lost in the
     * mists of history: the commit which originally introduced this
     * change on 2001-05-06 was clear on _what_ it was doing but
     * didn't bother to explain _why_. But I [SGT, 2017-12-03] suspect
     * it has to do with priority order: what should a saved session
     * do if its CONF_host contains 'server.example.com:123' and its
     * CONF_port contains 456? If CONF_port contained the _default_
     * port number then it might be a good guess that the colon suffix
     * on the host name was intended to override that, but you don't
     * really want to get into making heuristic judgments on that
     * basis.
     *
     * (Then again, you could just as easily make the same argument
     * about whether a 'user@' prefix on the host name should override
     * CONF_username, which this code _does_ do. I don't have a good
     * answer, sadly. Both these pieces of behaviour have been around
     * for years and it would probably cause subtle breakage in all
     * sorts of long-forgotten scripting to go changing things around
     * now.)
     *
     * In order to protect unbracketed IPv6 address literals against
     * this treatment, we do not make this change at all if there's
     * _more_ than one (un-IPv6-bracketed) colon.
     */
    p = host_strchr(host, ':');
    if (p && p != host_strrchr(host, ':')) {
        *p = '\0';
    }

    /*
     * Remove any remaining whitespace.
     */
    p = hostbuf;
    q = host;
    while (*q) {
        if (*q != ' ' && *q != '\t')
            *p++ = *q;
        q++;
    }
    *p = '\0';

    conf_set_str(conf, CONF_host, hostbuf);
    sfree(hostbuf);
}
