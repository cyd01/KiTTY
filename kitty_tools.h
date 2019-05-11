#ifndef KITTY_TOOLS
#define KITTY_TOOLS

#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include <sys/stat.h>
#include <ctype.h>

#ifndef MAX_VALUE_NAME
#define MAX_VALUE_NAME 16383
#endif

// Procedures de traitement de chaines
int stricmp(const char *s1, const char *s2) ;

char *stristr (const char *meule_de_foin, const char *aiguille) ;

/* Fonction permettant d'inserer une chaine dans une autre */
int insert( char * ch, const char * c, const int ipos ) ;

/* Fonction permettant de supprimer une partie d'une chaine de caracteres */
int del( char * ch, const int start, const int length ) ;

/* Fonction permettant de retrouver la position d'une chaine dans une autre chaine */
int poss( const char * c, const char * ch ) ;

/* Fonction permettant de retrouver la position d'une chaîne de caracteres dans une chaine a partir d'une position donnee */
int posi( const char * c, const char * ch, const int ipos ) ;

// Teste l'existance d'un fichier
int existfile( const char * filename ) ;

// Teste l'existance d'un repertoire
int existdirectory( const char * filename ) ;

/* Donne la taille d'un fichier */
long filesize( const char * filename ) ;

// Supprime les double anti-slash
void DelDoubleBackSlash( char * st ) ;
	
// Ajoute une chaine dans une liste de chaines
int StringList_Add( char **list, const char *str ) ;

// Test si une chaine existe dans une liste de chaines
int StringList_Exist( const char **list, const char * name ) ;

// Supprime une chaine d'une liste de chaines
void StringList_Del( char **list, const char * name ) ;

// Reorganise l'ordre d'une liste de chaines en montant la chaine selectionnee d'un cran
void StringList_Up( char **list, const char * name ) ;

// Positionne l'environnement
int putenv (const char *string) ;
int set_env( char * name, char * value ) ;
int add_env( char * name, char * value ) ;

// Creer un repertoire recurssif (rep1 / rep2 / ...)
int MakeDir( const char * directory ) ;

// Affichage d'un message dans l'event log
void debug_logevent( const char *fmt, ... ) ;
#endif
