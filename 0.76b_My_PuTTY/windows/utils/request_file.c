/*
 * GetOpenFileName/GetSaveFileName tend to muck around with the process'
 * working directory on at least some versions of Windows.
 * Here's a wrapper that gives more control over this, and hides a little
 * bit of other grottiness.
 */

#include "putty.h"

struct filereq_tag {
    TCHAR cwd[MAX_PATH];
};

/*
 * `of' is expected to be initialised with most interesting fields, but
 * this function does some administrivia. (assume `of' was memset to 0)
 * save==1 -> GetSaveFileName; save==0 -> GetOpenFileName
 * `state' is optional.
 */
bool request_file(filereq *state, OPENFILENAME *of, bool preserve, bool save)
{
    TCHAR cwd[MAX_PATH]; /* process CWD */
    bool ret;

    /* Get process CWD */
    if (preserve) {
        DWORD r = GetCurrentDirectory(lenof(cwd), cwd);
        if (r == 0 || r >= lenof(cwd))
            /* Didn't work, oh well. Stop trying to be clever. */
            preserve = false;
    }

    /* Open the file requester, maybe setting lpstrInitialDir */
    {
#ifdef OPENFILENAME_SIZE_VERSION_400
        of->lStructSize = OPENFILENAME_SIZE_VERSION_400;
#else
        of->lStructSize = sizeof(*of);
#endif
        of->lpstrInitialDir = (state && state->cwd[0]) ? state->cwd : NULL;
        /* Actually put up the requester. */
        ret = save ? GetSaveFileName(of) : GetOpenFileName(of);
    }

    /* Get CWD left by requester */
    if (state) {
        DWORD r = GetCurrentDirectory(lenof(state->cwd), state->cwd);
        if (r == 0 || r >= lenof(state->cwd))
            /* Didn't work, oh well. */
            state->cwd[0] = '\0';
    }

    /* Restore process CWD */
    if (preserve)
        /* If it fails, there's not much we can do. */
        (void) SetCurrentDirectory(cwd);

    return ret;
}

filereq *filereq_new(void)
{
    filereq *ret = snew(filereq);
    ret->cwd[0] = '\0';
    return ret;
}

void filereq_free(filereq *state)
{
    sfree(state);
}
