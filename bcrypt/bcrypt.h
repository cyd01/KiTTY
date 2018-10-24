
#ifndef __BCRYPT
#define __BCRYPT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Cas de la construction de la DLL */
#ifdef BUILD_DLL
#define LINKDLL extern "C" __declspec(dllexport)
#define LINKDLLCPP __declspec(dllexport)
#else
#define LINKDLL
#define LINKDLLCPP
#endif

/* code retour
1 si tout va bien
0 en cas d'erreur
*/

LINKDLL void bcrypt_init( const long t ) ;

LINKDLL int bcrypt_string( const char * st_in, char * st_out, const unsigned int length
	, const char * init_pattern, const char * key, const unsigned int maxlinesize ) ;
LINKDLL int buncrypt_string( const char * st_in, char * st_out, const unsigned int length
	, const char * init_pattern, const char * key ) ;

LINKDLL int bcrypt_string_printable( const char * st_in, char * st_out
	, const unsigned int length, const char * key, const unsigned int maxlinesize ) ;
LINKDLL int buncrypt_string_printable( const char * st_in, char * st_out
	, const unsigned int length, const char * key ) ;

LINKDLL int bcrypt_string_base64( const char * st_in, char * st_out
	, const unsigned int length, const char * key, const unsigned int maxlinesize ) ;
LINKDLL int buncrypt_string_base64( const char * st_in, char * st_out
	, const unsigned int length, const char * key ) ;

LINKDLL int bcrypt_string_auto( char * st, const unsigned int length
	, const char * init_pattern, const char * key, const unsigned int maxlinesize ) ;
LINKDLL int buncrypt_string_auto( char * st, const unsigned int length
	, const char * init_pattern, const char * key ) ;

LINKDLL int bcrypt_file( const char * filename_in, const char * filename_out
	, const char * init_pattern, const char * key, const int unsigned maxlinesize ) ;
LINKDLL int buncrypt_file( const char * filename_in, const char * filename_out
	, const char * init_pattern, const char * key ) ;

LINKDLL int bcrypt_file_printable( const char * filename_in, const char * filename_out
	, const char * key, const unsigned int maxlinesize ) ;
LINKDLL int buncrypt_file_printable( const char * filename, const char * filename_out
	, const char * key ) ;

LINKDLL int bcrypt_file_base64( const char * filename_in, const char * filename_out
	, const char * key, const unsigned int maxlinesize ) ;
LINKDLL int buncrypt_file_base64( const char * filename, const char * filename_out
	, const char * key ) ;

LINKDLL int bcrypt_file_auto( const char * filename
	, const char * init_pattern, const char * key, const int unsigned maxlinesize ) ;
LINKDLL int buncrypt_file_auto( const char * filename
	, const char * init_pattern, const char * key ) ;

#endif
