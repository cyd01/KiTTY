/*
    Fichier base64.c
    Auteur Bernard Chardonneau
    améliorations encode64 proposées par big.lol@free.fr

    Logiciel libre, droits d'utilisation précisés en français
    dans le fichier : licence.fr

    Traductions des droits d'utilisation dans les fichiers :
    licence.de , licence.en , licence.es , licence.it
    licence.nl , licence.pt , licence.eo , licence.eo-utf


    Bibliothèque de fonctions permettant d'encoder et de
    décoder le contenu d'un tableau en base64.
*/

#include "base64.h"


/* encode base64 nbcar caractères mémorisés
   dans orig et met le résultat dans dest */

void encode64 (char *orig, char *dest, int nbcar)
{
    // groupe de 3 octets à convertir en base 64
    unsigned char octet1, octet2, octet3;

    // tableau d'encodage
    // ce tableau est statique pour éviter une allocation
    // mémoire + initialisation à chaque appel de la fonction
    static char   valcar [] =
          "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


    // tant qu'il reste au moins 3 caractères à encoder
    while (nbcar >= 3)
    {
        // extraire 3 caractères de la chaine et les
        // mémoriser sous la forme d'octets (non sigés)
        octet1 = *(orig++);
        octet2 = *(orig++);
        octet3 = *(orig++);

        // décomposer les 3 octets en tranches de 6 bits et les
        // remplacer par les caractères correspondants dans valcar
        *(dest++) = valcar [octet1 >> 2];
        *(dest++) = valcar [((octet1 & 3) << 4) | (octet2 >> 4)];
        *(dest++) = valcar [((octet2 & 0x0F) << 2) | (octet3 >> 6)];
        *(dest++) = valcar [octet3 & 0x3F];

        // 3 caractères de moins à traiter
        nbcar -= 3;
    }

    // s'il reste des caractères à encoder
    if (nbcar)
    {
        // encodage des 6 bits de poids fort du premier caractère
        octet1 = *(orig++);
        *(dest++) = valcar [octet1 >> 2];

        // s'il ne reste que ce caractère à encoder
        if (nbcar == 1)
        {
            // encodage des 2 bits de poids faible de ce ce caractère
            *(dest++) = valcar [(octet1 & 3) << 4];

            // indique qu'aucun autre caractère n'est encodé
            *(dest++) = '=';
        }
        // sinon (il reste 2 caractères à encoder)
        else
        {
            // 2 bits de poids faible du 1er caractère + encodage de l'autre
            octet2 = *orig;
            *(dest++) = valcar [((octet1 & 3) << 4) | (octet2 >> 4)];
            *(dest++) = valcar [(octet2 & 0x0F) << 2];
        }

        // indique qu'aucun autre caractère n'est encodé
        *(dest++) = '=';
    }

    // fin de l'encodage
    *dest = '\0';
}



/* décode le contenu de buffer encodé base64, met le résultat
   dans buffer et retourne le nombre de caractères convertis */

int decode64 (char *buffer)
{
    int  car;        // caractère du fichier
    char valcar[4]=""; // valeur après conversion des caractères
    int  i;          // compteur
    int  posorig;    // position dans la ligne passée en paramètre
    int  posdest;    // position dans la nouvelle ligne générée


    // initialisations
    posorig = 0;
    posdest = 0;

    // tant que non fin de ligne
    while (buffer [posorig] > ' ' && buffer [posorig] != '=')
    {
        // décoder la valeur de 4 caractères
        for (i = 0; i < 4 && buffer [posorig] != '='; i++)
        {
            // récupérer un caractère dans la ligne
            car = buffer [posorig++];

            // décoder ce caractère
            if ('A' <= car && car <= 'Z')
                valcar [i] = car - 'A';
            else if ('a' <= car && car <= 'z')
                valcar [i] = car + 26 - 'a';
            else if ('0' <= car && car <= '9')
                valcar [i] = car + 52 - '0';
            else if (car == '+')
                valcar [i] = 62;
            else if (car == '/')
                valcar [i] = 63;
        }

        // recopier les caractères correspondants dans le buffer
        buffer [posdest++] = (valcar [0] << 2) | (valcar [1] >> 4);

        // sauf si indicateur de fin de message
        if (i > 2)
        {
            buffer [posdest++] = (valcar [1] << 4) | (valcar [2] >> 2);

            if (i > 3)
                buffer [posdest++] = (valcar [2] << 6) | (valcar [3]);
        }
    }

    // terminer le buffer
    buffer [posdest] = '\0';

    // et retourner le nombre de caractères obtenus
    return (posdest);
}
