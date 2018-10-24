/*
 * wincapi.h: Windows Crypto API functions defined in wincrypt.c
 * that use the crypt32 library. Also centralises the machinery
 * for dynamically loading that library.
 */

#if !defined NO_SECURITY

#ifndef WINCAPI_GLOBAL
#define WINCAPI_GLOBAL extern
#endif

DECL_WINDOWS_FUNCTION(WINCAPI_GLOBAL, BOOL, CryptProtectMemory,
		      (LPVOID,DWORD,DWORD));

int got_crypt(void);

#endif
