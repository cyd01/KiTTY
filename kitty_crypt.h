#ifndef KITTYCRYPT_H
#define KITTYCRYPT_H

#include "nbcrypt.h"

extern char PassKey[1024] ;

int cryptstring( char * st, const char * key ) ;
int decryptstring( char * st, const char * key ) ;

// Generation de la clé privée PuTTY
int GenerePrivateKey( const char * filename ) ;

// Procedure de management de la passphrase
int GetUserPassSSHNoSave(void) ;
char * ManagePassPhrase( const char * st ) ;

#endif
