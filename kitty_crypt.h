#ifndef KITTYCRYPT_H
#define KITTYCRYPT_H

#include "nbcrypt.h"

int cryptstring( char * st, const char * key ) ;
int decryptstring( char * st, const char * key ) ;
int cryptpassword( int mode, char * password, const char * host, const char * termtype ) ;
int decryptpassword( int mode, char * password, const char * host, const char * termtype ) ;

// Procedure de management de la passphrase
int GetUserPassSSHNoSave(void) ;
char * ManagePassPhrase( const char * st ) ;

#endif
