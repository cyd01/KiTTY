
#ifndef __MINI
#define __MINI

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef WIN32
#include <io.h>
#include <sys/locking.h>
#else
#include <sys/file.h>
#define _locking lockf
#define LK_LOCK F_LOCK
#define LK_UNLCK F_ULOCK
#endif


typedef struct s_section {
	int type ;
	char * name ;
	struct s_section * next ; 	// section suivante
	struct s_key * first ;		// première clé
	} SSECTION ;

typedef struct s_key {
	int type ;
	char * name ;
	void * value ;
	struct s_key * next ;
	} SKEY ;

typedef struct s_ini {
	char * name ;
	struct s_section * first ;
	} SINI ;
	
SSECTION * newSECTION( const char * name ) ;

SKEY * newKEY( const char * name, const char * value ) ;

SINI * newINI( void ) ;

void freeSECTION( SSECTION ** Section ) ;

void freeKEY( SKEY ** Key ) ;

void freeINI( SINI ** Ini ) ;

int addSECTION( SSECTION * Section, SSECTION * SectionAdd ) ;

int addINI( SINI * Ini, SSECTION * Section ) ;	

int delSECTION( SSECTION * Section, const char * name ) ;

int addKEY( SSECTION * Section, SKEY * Key ) ;

int delKEY( SSECTION * Section, const char * name ) ;

SSECTION * getSECTION( SSECTION * Section, const char * name ) ;

SSECTION * lastSECTION( SSECTION * Section ) ;

SKEY * getKEY( SSECTION * Section, const char * name ) ;

char * getvalueKEY( SKEY * Key ) ;

int setKEY( SKEY * Key, char * value ) ;

void printKEY( SKEY * Key ) ;

void printSECTION( SSECTION * Section ) ;

void printINI( SINI * Ini ) ;

int loadINI( SINI * Ini, const char * fileName ) ;

int readINI( const char * filename, const char * section, const char * key, char * pStr) ;

int storeKEY( SKEY * Key, FILE * fp ) ;

int storeSECTION( SSECTION * Section, FILE * fp ) ;

int storeINI( SINI * Ini, const char * filename ) ;

int writeINI( const char * filename, const char * section, const char * key, const char * value ) ;

int writeINISec( const char * filename, const char * section, const char * key, const char * value ) ;

int delINI( const char * filename, const char * section, const char * key ) ;

int delINISec( const char * filename, const char * section, const char * key ) ;

#endif
