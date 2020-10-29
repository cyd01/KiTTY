#ifndef KITTYCRYPT_H
#define KITTYCRYPT_H

#include "nbcrypt.h"
#include <windows.h>

int cryptstring( const int mode, char * st, const char * key ) ;
int decryptstring( const int mode, char * st, const char * key ) ;
int cryptpassword( const int mode, char * password, const char * host, const char * termtype ) ;
int decryptpassword( const int mode, char * password, const char * host, const char * termtype ) ;

void MASKPASS( const int mode, char * password ) ;

// Procedure de management de la passphrase
int GetUserPassSSHNoSave(void) ;
char * ManagePassPhrase( const char * st ) ;
size_t iso8859_1_to_utf8(char *content, size_t max_size) ; 					// Latin-1 = iso8859-1
size_t utf8_to_iso8859_15(char *const output, const char *const input, const size_t length) ;   // Latin-9 = iso8859-15

#endif
