/*
   Fichier base64.h
   Auteur Bernard Chardonneau
*/

/* prototypes des fonctions de la bibliothèque base64 */

void encode64 (char *orig, char *dest, int nbcar);
int decode64 (char *buffer);