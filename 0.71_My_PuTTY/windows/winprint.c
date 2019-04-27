/*
 * Printing interface for PuTTY.
 */

#include "putty.h"
#include <winspool.h>

struct printer_enum_tag {
    int nprinters;
    DWORD enum_level;
    union {
	LPPRINTER_INFO_4 i4;
	LPPRINTER_INFO_5 i5;
    } info;
};

struct printer_job_tag {
    HANDLE hprinter;
};

DECL_WINDOWS_FUNCTION(static, BOOL, EnumPrinters,
                      (DWORD, LPTSTR, DWORD, LPBYTE, DWORD, LPDWORD, LPDWORD));
DECL_WINDOWS_FUNCTION(static, BOOL, OpenPrinter,
                      (LPTSTR, LPHANDLE, LPPRINTER_DEFAULTS));
DECL_WINDOWS_FUNCTION(static, BOOL, ClosePrinter, (HANDLE));
DECL_WINDOWS_FUNCTION(static, DWORD, StartDocPrinter, (HANDLE, DWORD, LPBYTE));
DECL_WINDOWS_FUNCTION(static, BOOL, EndDocPrinter, (HANDLE));
DECL_WINDOWS_FUNCTION(static, BOOL, StartPagePrinter, (HANDLE));
DECL_WINDOWS_FUNCTION(static, BOOL, EndPagePrinter, (HANDLE));
DECL_WINDOWS_FUNCTION(static, BOOL, WritePrinter,
                      (HANDLE, LPVOID, DWORD, LPDWORD));

static void init_winfuncs(void)
{
    static bool initialised = false;
    if (initialised)
        return;
    {
        HMODULE winspool_module = load_system32_dll("winspool.drv");
        /* Some MSDN documentation claims that some of the below functions
         * should be loaded from spoolss.dll, but this doesn't seem to
         * be reliable in practice.
         * Nevertheless, we load spoolss.dll ourselves using our safe
         * loading method, against the possibility that winspool.drv
         * later loads it unsafely. */
        (void) load_system32_dll("spoolss.dll");
        GET_WINDOWS_FUNCTION_PP(winspool_module, EnumPrinters);
        GET_WINDOWS_FUNCTION_PP(winspool_module, OpenPrinter);
        GET_WINDOWS_FUNCTION_PP(winspool_module, ClosePrinter);
        GET_WINDOWS_FUNCTION_PP(winspool_module, StartDocPrinter);
        GET_WINDOWS_FUNCTION_PP(winspool_module, EndDocPrinter);
        GET_WINDOWS_FUNCTION_PP(winspool_module, StartPagePrinter);
        GET_WINDOWS_FUNCTION_PP(winspool_module, EndPagePrinter);
        GET_WINDOWS_FUNCTION_PP(winspool_module, WritePrinter);
    }
    initialised = true;
}

static bool printer_add_enum(int param, DWORD level, char **buffer,
                             int offset, int *nprinters_ptr)
{
    DWORD needed = 0, nprinters = 0;

    init_winfuncs();

    *buffer = sresize(*buffer, offset+512, char);

    /*
     * Exploratory call to EnumPrinters to determine how much space
     * we'll need for the output. Discard the return value since it
     * will almost certainly be a failure due to lack of space.
     */
    p_EnumPrinters(param, NULL, level, (LPBYTE)((*buffer)+offset), 512,
                   &needed, &nprinters);

    if (needed < 512)
        needed = 512;

    *buffer = sresize(*buffer, offset+needed, char);

    if (p_EnumPrinters(param, NULL, level, (LPBYTE)((*buffer)+offset),
                       needed, &needed, &nprinters) == 0)
        return false;

    *nprinters_ptr += nprinters;

    return true;
}

printer_enum *printer_start_enum(int *nprinters_ptr)
{
    printer_enum *ret = snew(printer_enum);
    char *buffer = NULL;

    *nprinters_ptr = 0;		       /* default return value */
    buffer = snewn(512, char);

    /*
     * Determine what enumeration level to use.
     * When enumerating printers, we need to use PRINTER_INFO_4 on
     * NT-class systems to avoid Windows looking too hard for them and
     * slowing things down; and we need to avoid PRINTER_INFO_5 as
     * we've seen network printers not show up.
     * On 9x-class systems, PRINTER_INFO_4 isn't available and
     * PRINTER_INFO_5 is recommended.
     * Bletch.
     */
    if (osPlatformId != VER_PLATFORM_WIN32_NT) {
	ret->enum_level = 5;
    } else {
	ret->enum_level = 4;
    }

    if (!printer_add_enum(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
                          ret->enum_level, &buffer, 0, nprinters_ptr))
        goto error;

    switch (ret->enum_level) {
      case 4:
	ret->info.i4 = (LPPRINTER_INFO_4)buffer;
	break;
      case 5:
	ret->info.i5 = (LPPRINTER_INFO_5)buffer;
	break;
    }
    ret->nprinters = *nprinters_ptr;
    
    return ret;

    error:
    sfree(buffer);
    sfree(ret);
    *nprinters_ptr = 0;
    return NULL;
}

char *printer_get_name(printer_enum *pe, int i)
{
    if (!pe)
	return NULL;
    if (i < 0 || i >= pe->nprinters)
	return NULL;
    switch (pe->enum_level) {
      case 4:
	return pe->info.i4[i].pPrinterName;
      case 5:
	return pe->info.i5[i].pPrinterName;
      default:
	return NULL;
    }
}

void printer_finish_enum(printer_enum *pe)
{
    if (!pe)
	return;
    switch (pe->enum_level) {
      case 4:
	sfree(pe->info.i4);
	break;
      case 5:
	sfree(pe->info.i5);
	break;
    }
    sfree(pe);
}

printer_job *printer_start_job(char *printer)
{
    printer_job *ret = snew(printer_job);
    DOC_INFO_1 docinfo;
    bool jobstarted = false, pagestarted = false;

    init_winfuncs();

    ret->hprinter = NULL;
    if (!p_OpenPrinter(printer, &ret->hprinter, NULL))
	goto error;

    docinfo.pDocName = "PuTTY remote printer output";
    docinfo.pOutputFile = NULL;
    docinfo.pDatatype = "RAW";

    if (!p_StartDocPrinter(ret->hprinter, 1, (LPBYTE)&docinfo))
	goto error;
    jobstarted = true;

    if (!p_StartPagePrinter(ret->hprinter))
	goto error;
    pagestarted = true;

    return ret;

    error:
    if (pagestarted)
	p_EndPagePrinter(ret->hprinter);
    if (jobstarted)
	p_EndDocPrinter(ret->hprinter);
    if (ret->hprinter)
	p_ClosePrinter(ret->hprinter);
    sfree(ret);
    return NULL;
}

void printer_job_data(printer_job *pj, const void *data, size_t len)
{
    DWORD written;

    if (!pj)
	return;

    p_WritePrinter(pj->hprinter, (void *)data, len, &written);
}

void printer_finish_job(printer_job *pj)
{
    if (!pj)
	return;

    p_EndPagePrinter(pj->hprinter);
    p_EndDocPrinter(pj->hprinter);
    p_ClosePrinter(pj->hprinter);
    sfree(pj);
}
