/*
 * winjump.c: support for Windows 7 jump lists.
 *
 * The Windows 7 jumplist is a customizable list defined by the
 * application. It is persistent across application restarts: the OS
 * maintains the list when the app is not running. The list is shown
 * when the user right-clicks on the taskbar button of a running app
 * or a pinned non-running application. We use the jumplist to
 * maintain a list of recently started saved sessions, started either
 * by doubleclicking on a saved session, or with the command line
 * "-load" parameter.
 *
 * Since the jumplist is write-only: it can only be replaced and the
 * current list cannot be read, we must maintain the contents of the
 * list persistantly in the registry. The file winstore.h contains
 * functions to directly manipulate these registry entries. This file
 * contains higher level functions to manipulate the jumplist.
 */

#include <assert.h>

#include "putty.h"
#include "storage.h"

#define MAX_JUMPLIST_ITEMS 30 /* PuTTY will never show more items in
                               * the jumplist than this, regardless of
                               * user preferences. */

/*
 * COM structures and functions.
 */
#ifndef PROPERTYKEY_DEFINED
#define PROPERTYKEY_DEFINED
typedef struct _tagpropertykey {
    GUID fmtid;
    DWORD pid;
} PROPERTYKEY;
#endif
#ifndef _REFPROPVARIANT_DEFINED
#define _REFPROPVARIANT_DEFINED
typedef PROPVARIANT *REFPROPVARIANT;
#endif
/* MinGW doesn't define this yet: */
#ifndef _PROPVARIANTINIT_DEFINED_
#define _PROPVARIANTINIT_DEFINED_
#define PropVariantInit(pvar) memset((pvar),0,sizeof(PROPVARIANT))
#endif

#define IID_IShellLink IID_IShellLinkA

typedef struct ICustomDestinationListVtbl {
    HRESULT ( __stdcall *QueryInterface ) (
        /* [in] ICustomDestinationList*/ void *This,
        /* [in] */  const GUID * const riid,
        /* [out] */ void **ppvObject);

    ULONG ( __stdcall *AddRef )(
        /* [in] ICustomDestinationList*/ void *This);

    ULONG ( __stdcall *Release )(
        /* [in] ICustomDestinationList*/ void *This);

    HRESULT ( __stdcall *SetAppID )(
        /* [in] ICustomDestinationList*/ void *This,
        /* [string][in] */ LPCWSTR pszAppID);

    HRESULT ( __stdcall *BeginList )(
        /* [in] ICustomDestinationList*/ void *This,
        /* [out] */ UINT *pcMinSlots,
        /* [in] */  const GUID * const riid,
        /* [out] */ void **ppv);

    HRESULT ( __stdcall *AppendCategory )(
        /* [in] ICustomDestinationList*/ void *This,
        /* [string][in] */ LPCWSTR pszCategory,
        /* [in] IObjectArray*/ void *poa);

    HRESULT ( __stdcall *AppendKnownCategory )(
        /* [in] ICustomDestinationList*/ void *This,
        /* [in] KNOWNDESTCATEGORY*/ int category);

    HRESULT ( __stdcall *AddUserTasks )(
        /* [in] ICustomDestinationList*/ void *This,
        /* [in] IObjectArray*/ void *poa);

    HRESULT ( __stdcall *CommitList )(
        /* [in] ICustomDestinationList*/ void *This);

    HRESULT ( __stdcall *GetRemovedDestinations )(
        /* [in] ICustomDestinationList*/ void *This,
        /* [in] */ const IID * const riid,
        /* [out] */ void **ppv);

    HRESULT ( __stdcall *DeleteList )(
        /* [in] ICustomDestinationList*/ void *This,
        /* [string][unique][in] */ LPCWSTR pszAppID);

    HRESULT ( __stdcall *AbortList )(
        /* [in] ICustomDestinationList*/ void *This);

} ICustomDestinationListVtbl;

typedef struct ICustomDestinationList
{
    ICustomDestinationListVtbl *lpVtbl;
} ICustomDestinationList;

typedef struct IObjectArrayVtbl
{
    HRESULT ( __stdcall *QueryInterface )(
        /* [in] IObjectArray*/ void *This,
        /* [in] */ const GUID * const riid,
        /* [out] */ void **ppvObject);

    ULONG ( __stdcall *AddRef )(
        /* [in] IObjectArray*/ void *This);

    ULONG ( __stdcall *Release )(
        /* [in] IObjectArray*/ void *This);

    HRESULT ( __stdcall *GetCount )(
        /* [in] IObjectArray*/ void *This,
        /* [out] */ UINT *pcObjects);

    HRESULT ( __stdcall *GetAt )(
        /* [in] IObjectArray*/ void *This,
        /* [in] */ UINT uiIndex,
        /* [in] */ const GUID * const riid,
        /* [out] */ void **ppv);

} IObjectArrayVtbl;

typedef struct IObjectArray
{
    IObjectArrayVtbl *lpVtbl;
} IObjectArray;

typedef struct IShellLinkVtbl
{
    HRESULT ( __stdcall *QueryInterface )(
        /* [in] IShellLink*/ void *This,
        /* [in] */ const GUID * const riid,
        /* [out] */ void **ppvObject);

    ULONG ( __stdcall *AddRef )(
        /* [in] IShellLink*/ void *This);

    ULONG ( __stdcall *Release )(
        /* [in] IShellLink*/ void *This);

    HRESULT ( __stdcall *GetPath )(
        /* [in] IShellLink*/ void *This,
        /* [string][out] */ LPSTR pszFile,
        /* [in] */ int cch,
        /* [unique][out][in] */ WIN32_FIND_DATAA *pfd,
        /* [in] */ DWORD fFlags);

    HRESULT ( __stdcall *GetIDList )(
        /* [in] IShellLink*/ void *This,
        /* [out] LPITEMIDLIST*/ void **ppidl);

    HRESULT ( __stdcall *SetIDList )(
        /* [in] IShellLink*/ void *This,
        /* [in] LPITEMIDLIST*/ void *pidl);

    HRESULT ( __stdcall *GetDescription )(
        /* [in] IShellLink*/ void *This,
        /* [string][out] */ LPSTR pszName,
        /* [in] */ int cch);

    HRESULT ( __stdcall *SetDescription )(
        /* [in] IShellLink*/ void *This,
        /* [string][in] */ LPCSTR pszName);

    HRESULT ( __stdcall *GetWorkingDirectory )(
        /* [in] IShellLink*/ void *This,
        /* [string][out] */ LPSTR pszDir,
        /* [in] */ int cch);

    HRESULT ( __stdcall *SetWorkingDirectory )(
        /* [in] IShellLink*/ void *This,
        /* [string][in] */ LPCSTR pszDir);

    HRESULT ( __stdcall *GetArguments )(
        /* [in] IShellLink*/ void *This,
        /* [string][out] */ LPSTR pszArgs,
        /* [in] */ int cch);

    HRESULT ( __stdcall *SetArguments )(
        /* [in] IShellLink*/ void *This,
        /* [string][in] */ LPCSTR pszArgs);

    HRESULT ( __stdcall *GetHotkey )(
        /* [in] IShellLink*/ void *This,
        /* [out] */ WORD *pwHotkey);

    HRESULT ( __stdcall *SetHotkey )(
        /* [in] IShellLink*/ void *This,
        /* [in] */ WORD wHotkey);

    HRESULT ( __stdcall *GetShowCmd )(
        /* [in] IShellLink*/ void *This,
        /* [out] */ int *piShowCmd);

    HRESULT ( __stdcall *SetShowCmd )(
        /* [in] IShellLink*/ void *This,
        /* [in] */ int iShowCmd);

    HRESULT ( __stdcall *GetIconLocation )(
        /* [in] IShellLink*/ void *This,
        /* [string][out] */ LPSTR pszIconPath,
        /* [in] */ int cch,
        /* [out] */ int *piIcon);

    HRESULT ( __stdcall *SetIconLocation )(
        /* [in] IShellLink*/ void *This,
        /* [string][in] */ LPCSTR pszIconPath,
        /* [in] */ int iIcon);

    HRESULT ( __stdcall *SetRelativePath )(
        /* [in] IShellLink*/ void *This,
        /* [string][in] */ LPCSTR pszPathRel,
        /* [in] */ DWORD dwReserved);

    HRESULT ( __stdcall *Resolve )(
        /* [in] IShellLink*/ void *This,
        /* [unique][in] */ HWND hwnd,
        /* [in] */ DWORD fFlags);

    HRESULT ( __stdcall *SetPath )(
        /* [in] IShellLink*/ void *This,
        /* [string][in] */ LPCSTR pszFile);

} IShellLinkVtbl;

typedef struct IShellLink
{
    IShellLinkVtbl *lpVtbl;
} IShellLink;

typedef struct IObjectCollectionVtbl
{
    HRESULT ( __stdcall *QueryInterface )(
        /* [in] IShellLink*/ void *This,
        /* [in] */ const GUID * const riid,
        /* [out] */ void **ppvObject);

    ULONG ( __stdcall *AddRef )(
        /* [in] IShellLink*/ void *This);

    ULONG ( __stdcall *Release )(
        /* [in] IShellLink*/ void *This);

    HRESULT ( __stdcall *GetCount )(
        /* [in] IShellLink*/ void *This,
        /* [out] */ UINT *pcObjects);

    HRESULT ( __stdcall *GetAt )(
        /* [in] IShellLink*/ void *This,
        /* [in] */ UINT uiIndex,
        /* [in] */ const GUID * const riid,
        /* [iid_is][out] */ void **ppv);

    HRESULT ( __stdcall *AddObject )(
        /* [in] IShellLink*/ void *This,
        /* [in] */ void *punk);

    HRESULT ( __stdcall *AddFromArray )(
        /* [in] IShellLink*/ void *This,
        /* [in] */ IObjectArray *poaSource);

    HRESULT ( __stdcall *RemoveObjectAt )(
        /* [in] IShellLink*/ void *This,
        /* [in] */ UINT uiIndex);

    HRESULT ( __stdcall *Clear )(
        /* [in] IShellLink*/ void *This);

} IObjectCollectionVtbl;

typedef struct IObjectCollection
{
    IObjectCollectionVtbl *lpVtbl;
} IObjectCollection;

typedef struct IPropertyStoreVtbl
{
    HRESULT ( __stdcall *QueryInterface )(
        /* [in] IPropertyStore*/ void *This,
        /* [in] */ const GUID * const riid,
        /* [iid_is][out] */ void **ppvObject);

    ULONG ( __stdcall *AddRef )(
        /* [in] IPropertyStore*/ void *This);

    ULONG ( __stdcall *Release )(
        /* [in] IPropertyStore*/ void *This);

    HRESULT ( __stdcall *GetCount )(
        /* [in] IPropertyStore*/ void *This,
        /* [out] */ DWORD *cProps);

    HRESULT ( __stdcall *GetAt )(
        /* [in] IPropertyStore*/ void *This,
        /* [in] */ DWORD iProp,
        /* [out] */ PROPERTYKEY *pkey);

    HRESULT ( __stdcall *GetValue )(
        /* [in] IPropertyStore*/ void *This,
        /* [in] */ const PROPERTYKEY * const key,
        /* [out] */ PROPVARIANT *pv);

    HRESULT ( __stdcall *SetValue )(
        /* [in] IPropertyStore*/ void *This,
        /* [in] */ const PROPERTYKEY * const key,
        /* [in] */ REFPROPVARIANT propvar);

    HRESULT ( __stdcall *Commit )(
        /* [in] IPropertyStore*/ void *This);
} IPropertyStoreVtbl;

typedef struct IPropertyStore
{
    IPropertyStoreVtbl *lpVtbl;
} IPropertyStore;

static const CLSID CLSID_DestinationList = {
    0x77f10cf0, 0x3db5, 0x4966, {0xb5,0x20,0xb7,0xc5,0x4f,0xd3,0x5e,0xd6}
};
static const CLSID CLSID_ShellLink = {
    0x00021401, 0x0000, 0x0000, {0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}
};
static const CLSID CLSID_EnumerableObjectCollection = {
    0x2d3468c1, 0x36a7, 0x43b6, {0xac,0x24,0xd3,0xf0,0x2f,0xd9,0x60,0x7a}
};
static const IID IID_IObjectCollection = {
    0x5632b1a4, 0xe38a, 0x400a, {0x92,0x8a,0xd4,0xcd,0x63,0x23,0x02,0x95}
};
static const IID IID_IShellLink = {
    0x000214ee, 0x0000, 0x0000, {0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}
};
static const IID IID_ICustomDestinationList = {
    0x6332debf, 0x87b5, 0x4670, {0x90,0xc0,0x5e,0x57,0xb4,0x08,0xa4,0x9e}
};
static const IID IID_IObjectArray = {
    0x92ca9dcd, 0x5622, 0x4bba, {0xa8,0x05,0x5e,0x9f,0x54,0x1b,0xd8,0xc9}
};
static const IID IID_IPropertyStore = {
    0x886d8eeb, 0x8cf2, 0x4446, {0x8d,0x02,0xcd,0xba,0x1d,0xbd,0xcf,0x99}
};
static const PROPERTYKEY PKEY_Title = {
    {0xf29f85e0, 0x4ff9, 0x1068, {0xab,0x91,0x08,0x00,0x2b,0x27,0xb3,0xd9}},
    0x00000002
};

/* Type-checking macro to provide arguments for CoCreateInstance()
 * etc, ensuring that 'obj' really is a 'type **'. */
#define typecheck(checkexpr, result) \
    (sizeof(checkexpr) ? (result) : (result))
#define COMPTR(type, obj) &IID_##type, \
    typecheck((obj)-(type **)(obj), (void **)(void *)(obj))

static char putty_path[2048];

/*
 * Function to make an IShellLink describing a particular PuTTY
 * command. If 'appname' is null, the command run will be the one
 * returned by GetModuleFileName, i.e. our own executable; if it's
 * non-null then it will be assumed to be a filename in the same
 * directory as our own executable, and the return value will be NULL
 * if that file doesn't exist.
 *
 * If 'sessionname' is null then no command line will be passed to the
 * program. If it's non-null, the command line will be that text
 * prefixed with an @ (to load a PuTTY saved session).
 *
 * Hence, you can launch a saved session using make_shell_link(NULL,
 * sessionname), and launch another app using e.g.
 * make_shell_link("puttygen.exe", NULL).
 */
static IShellLink *make_shell_link(const char *appname,
                                   const char *sessionname)
{
    IShellLink *ret;
    char *app_path, *param_string, *desc_string;
    IPropertyStore *pPS;
    PROPVARIANT pv;

    /* Retrieve path to executable. */
    if (!putty_path[0])
        GetModuleFileName(NULL, putty_path, sizeof(putty_path) - 1);
    if (appname) {
        char *p, *q = putty_path;
        FILE *fp;

        if ((p = strrchr(q, '\\')) != NULL) q = p+1;
        if ((p = strrchr(q, ':')) != NULL) q = p+1;
        app_path = dupprintf("%.*s%s", (int)(q - putty_path), putty_path,
                             appname);
        if ((fp = fopen(app_path, "r")) == NULL) {
            sfree(app_path);
            return NULL;
        }
        fclose(fp);
    } else {
        app_path = dupstr(putty_path);
    }

    /* Check if this is a valid session, otherwise don't add. */
    if (sessionname) {
        settings_r *psettings_tmp = open_settings_r(sessionname);
        if (!psettings_tmp) {
            sfree(app_path);
            return NULL;
        }
        close_settings_r(psettings_tmp);
    }

    /* Create the new item. */
    if (!SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL,
                                    CLSCTX_INPROC_SERVER,
                                    COMPTR(IShellLink, &ret)))) {
        sfree(app_path);
        return NULL;
    }

    /* Set path, parameters, icon and description. */
    ret->lpVtbl->SetPath(ret, app_path);

    if (sessionname) {
        /* The leading space is reported to work around a Windows 10
         * behaviour change in which an argument string starting with
         * '@' causes the SetArguments method to silently do the wrong
         * thing. */
        param_string = dupcat(" @", sessionname);
    } else {
        param_string = dupstr("");
    }
    ret->lpVtbl->SetArguments(ret, param_string);
    sfree(param_string);

    if (sessionname) {
        desc_string = dupcat("Connect to PuTTY session '", sessionname, "'");
    } else {
        assert(appname);
        desc_string = dupprintf("Run %.*s",
                                (int)strcspn(appname, "."), appname);
    }
    ret->lpVtbl->SetDescription(ret, desc_string);
    sfree(desc_string);

    ret->lpVtbl->SetIconLocation(ret, app_path, 0);

    /* To set the link title, we require the property store of the link. */
    if (SUCCEEDED(ret->lpVtbl->QueryInterface(ret,
                                              COMPTR(IPropertyStore, &pPS)))) {
        PropVariantInit(&pv);
        pv.vt = VT_LPSTR;
        if (sessionname) {
            pv.pszVal = dupstr(sessionname);
        } else {
            assert(appname);
            pv.pszVal = dupprintf("Run %.*s",
                                  (int)strcspn(appname, "."), appname);
        }
        pPS->lpVtbl->SetValue(pPS, &PKEY_Title, &pv);
        sfree(pv.pszVal);
        pPS->lpVtbl->Commit(pPS);
        pPS->lpVtbl->Release(pPS);
    }

    sfree(app_path);

    return ret;
}

/* Updates jumplist from registry. */
static void update_jumplist_from_registry(void)
{
    const char *piterator;
    UINT num_items;
    int jumplist_counter;
    UINT nremoved;

    /* Variables used by the cleanup code must be initialised to NULL,
     * so that we don't try to free or release them if they were never
     * set up. */
    ICustomDestinationList *pCDL = NULL;
    char *pjumplist_reg_entries = NULL;
    IObjectCollection *collection = NULL;
    IObjectArray *array = NULL;
    IShellLink *link = NULL;
    IObjectArray *pRemoved = NULL;
    bool need_abort = false;

    /*
     * Create an ICustomDestinationList: the top-level object which
     * deals with jump list management.
     */
    if (!SUCCEEDED(CoCreateInstance(&CLSID_DestinationList, NULL,
                                    CLSCTX_INPROC_SERVER,
                                    COMPTR(ICustomDestinationList, &pCDL))))
        goto cleanup;

    /*
     * Call its BeginList method to start compiling a list. This gives
     * us back 'num_items' (a hint derived from systemwide
     * configuration about how many things to put on the list) and
     * 'pRemoved' (user configuration about things to leave off the
     * list).
     */
    if (!SUCCEEDED(pCDL->lpVtbl->BeginList(pCDL, &num_items,
                                           COMPTR(IObjectArray, &pRemoved))))
        goto cleanup;
    need_abort = true;
    if (!SUCCEEDED(pRemoved->lpVtbl->GetCount(pRemoved, &nremoved)))
        nremoved = 0;

    /*
     * Create an object collection to form the 'Recent Sessions'
     * category on the jump list.
     */
    if (!SUCCEEDED(CoCreateInstance(&CLSID_EnumerableObjectCollection,
                                    NULL, CLSCTX_INPROC_SERVER,
                                    COMPTR(IObjectCollection, &collection))))
        goto cleanup;

    /*
     * Go through the jump list entries from the registry and add each
     * one to the collection.
     */
    pjumplist_reg_entries = get_jumplist_registry_entries();
    piterator = pjumplist_reg_entries;
    jumplist_counter = 0;
    while (*piterator != '\0' &&
           (jumplist_counter < min(MAX_JUMPLIST_ITEMS, (int) num_items))) {
        link = make_shell_link(NULL, piterator);
        if (link) {
            UINT i;
            bool found;

            /*
             * Check that the link isn't in the user-removed list.
             */
            for (i = 0, found = false; i < nremoved && !found; i++) {
                IShellLink *rlink;
                if (SUCCEEDED(pRemoved->lpVtbl->GetAt
                              (pRemoved, i, COMPTR(IShellLink, &rlink)))) {
                    char desc1[2048], desc2[2048];
                    if (SUCCEEDED(link->lpVtbl->GetDescription
                                  (link, desc1, sizeof(desc1)-1)) &&
                        SUCCEEDED(rlink->lpVtbl->GetDescription
                                  (rlink, desc2, sizeof(desc2)-1)) &&
                        !strcmp(desc1, desc2)) {
                        found = true;
                    }
                    rlink->lpVtbl->Release(rlink);
                }
            }

            if (!found) {
                collection->lpVtbl->AddObject(collection, link);
                jumplist_counter++;
            }

            link->lpVtbl->Release(link);
            link = NULL;
        }
        piterator += strlen(piterator) + 1;
    }
    sfree(pjumplist_reg_entries);
    pjumplist_reg_entries = NULL;

    /*
     * Get the array form of the collection we've just constructed,
     * and put it in the jump list.
     */
    if (!SUCCEEDED(collection->lpVtbl->QueryInterface
                   (collection, COMPTR(IObjectArray, &array))))
        goto cleanup;

    pCDL->lpVtbl->AppendCategory(pCDL, L"Recent Sessions", array);

    /*
     * Create an object collection to form the 'Tasks' category on the
     * jump list.
     */
    if (!SUCCEEDED(CoCreateInstance(&CLSID_EnumerableObjectCollection,
                                    NULL, CLSCTX_INPROC_SERVER,
                                    COMPTR(IObjectCollection, &collection))))
        goto cleanup;

    /*
     * Add task entries for PuTTYgen and Pageant.
     */
    piterator = "Pageant.exe\0PuTTYgen.exe\0\0";
    while (*piterator != '\0') {
        link = make_shell_link(piterator, NULL);
        if (link) {
            collection->lpVtbl->AddObject(collection, link);
            link->lpVtbl->Release(link);
            link = NULL;
        }
        piterator += strlen(piterator) + 1;
    }

    /*
     * Get the array form of the collection we've just constructed,
     * and put it in the jump list.
     */
    if (!SUCCEEDED(collection->lpVtbl->QueryInterface
                   (collection, COMPTR(IObjectArray, &array))))
        goto cleanup;

    pCDL->lpVtbl->AddUserTasks(pCDL, array);

    /*
     * Now we can clean up the array and collection variables, so as
     * to be able to reuse them.
     */
    array->lpVtbl->Release(array);
    array = NULL;
    collection->lpVtbl->Release(collection);
    collection = NULL;

    /*
     * Create another object collection to form the user tasks
     * category.
     */
    if (!SUCCEEDED(CoCreateInstance(&CLSID_EnumerableObjectCollection,
                                    NULL, CLSCTX_INPROC_SERVER,
                                    COMPTR(IObjectCollection, &collection))))
        goto cleanup;

    /*
     * Get the array form of the collection we've just constructed,
     * and put it in the jump list.
     */
    if (!SUCCEEDED(collection->lpVtbl->QueryInterface
                   (collection, COMPTR(IObjectArray, &array))))
        goto cleanup;

    pCDL->lpVtbl->AddUserTasks(pCDL, array);

    /*
     * Now we can clean up the array and collection variables, so as
     * to be able to reuse them.
     */
    array->lpVtbl->Release(array);
    array = NULL;
    collection->lpVtbl->Release(collection);
    collection = NULL;

    /*
     * Commit the jump list.
     */
    pCDL->lpVtbl->CommitList(pCDL);
    need_abort = false;

    /*
     * Clean up.
     */
  cleanup:
    if (pRemoved) pRemoved->lpVtbl->Release(pRemoved);
    if (pCDL && need_abort) pCDL->lpVtbl->AbortList(pCDL);
    if (pCDL) pCDL->lpVtbl->Release(pCDL);
    if (collection) collection->lpVtbl->Release(collection);
    if (array) array->lpVtbl->Release(array);
    if (link) link->lpVtbl->Release(link);
    sfree(pjumplist_reg_entries);
}

/* Clears the entire jumplist. */
void clear_jumplist(void)
{
    ICustomDestinationList *pCDL;

    if (CoCreateInstance(&CLSID_DestinationList, NULL, CLSCTX_INPROC_SERVER,
                         COMPTR(ICustomDestinationList, &pCDL)) == S_OK) {
        pCDL->lpVtbl->DeleteList(pCDL, NULL);
        pCDL->lpVtbl->Release(pCDL);
    }

}

/* Adds a saved session to the Windows 7 jumplist. */
void add_session_to_jumplist(const char * const sessionname)
{
    if ((osMajorVersion < 6) || (osMajorVersion == 6 && osMinorVersion < 1))
        return;                        /* do nothing on pre-Win7 systems */

    if (add_to_jumplist_registry(sessionname) == JUMPLISTREG_OK) {
        update_jumplist_from_registry();
    } else {
        /* Make sure we don't leave the jumplist dangling. */
        clear_jumplist();
    }
}

/* Removes a saved session from the Windows jumplist. */
void remove_session_from_jumplist(const char * const sessionname)
{
    if ((osMajorVersion < 6) || (osMajorVersion == 6 && osMinorVersion < 1))
        return;                        /* do nothing on pre-Win7 systems */

    if (remove_from_jumplist_registry(sessionname) == JUMPLISTREG_OK) {
        update_jumplist_from_registry();
    } else {
        /* Make sure we don't leave the jumplist dangling. */
        clear_jumplist();
    }
}

/* Set Explicit App User Model Id to fix removable media error with
   jump lists */

bool set_explicit_app_user_model_id(void)
{
  DECL_WINDOWS_FUNCTION(static, HRESULT, SetCurrentProcessExplicitAppUserModelID,
                        (PCWSTR));

  static HMODULE shell32_module = 0;

    if (!shell32_module)
    {
        shell32_module = load_system32_dll("Shell32.dll");
        /*
         * We can't typecheck this function here, because it's defined
         * in <shobjidl.h>, which we're not including due to clashes
         * with all the manual-COM machinery above.
         */
        GET_WINDOWS_FUNCTION_NO_TYPECHECK(
            shell32_module, SetCurrentProcessExplicitAppUserModelID);
    }

    if (p_SetCurrentProcessExplicitAppUserModelID)
    {
#ifdef MOD_PERSO
        if (p_SetCurrentProcessExplicitAppUserModelID(L"9bis.com.KiTTY") == S_OK)
#else
        if (p_SetCurrentProcessExplicitAppUserModelID(L"SimonTatham.PuTTY") == S_OK)
#endif
        {
	  return true;
        }
        return false;
    }
    /* Function doesn't exist, which is ok for Pre-7 systems */

    return true;

}
