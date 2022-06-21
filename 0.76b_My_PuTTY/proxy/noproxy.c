/*
 * noproxy.c: an alternative to proxy.c, for use by auxiliary programs
 * that need to make network connections but don't want to include all
 * the full-on support for endless network proxies (and its
 * configuration requirements). Implements the primary APIs of
 * proxy.c, but maps them straight to the underlying network layer.
 */

#include "putty.h"
#include "network.h"
#include "proxy.h"

SockAddr *name_lookup(const char *host, int port, char **canonicalname,
                      Conf *conf, int addressfamily, LogContext *logctx,
                      const char *reason)
{
    return sk_namelookup(host, canonicalname, addressfamily);
}

Socket *new_connection(SockAddr *addr, const char *hostname,
                       int port, bool privport,
                       bool oobinline, bool nodelay, bool keepalive,
                       Plug *plug, Conf *conf)
{
    return sk_new(addr, port, privport, oobinline, nodelay, keepalive, plug);
}

Socket *new_listener(const char *srcaddr, int port, Plug *plug,
                     bool local_host_only, Conf *conf, int addressfamily)
{
    return sk_newlistener(srcaddr, port, plug, local_host_only, addressfamily);
}
