/*
 * aqsync.c: the agent_query_synchronous() wrapper function.
 *
 * This is a very small thing to have to put in its own module, but it
 * wants to be shared between back ends, and exist in any SSH client
 * program and also Pageant, and _nowhere else_ (because it pulls in
 * the main agent_query).
 */

#include <assert.h>

#include "putty.h"

void agent_query_synchronous(strbuf *query, void **out, int *outlen)
{
    agent_pending_query *pending;

    pending = agent_query(query, out, outlen, NULL, 0);
    assert(!pending);
}

