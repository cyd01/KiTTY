#ifndef __CONFIG_H__
  #define __CONFIG_H__

  #define WIN32_LEAN_AND_MEAN

  #define _WIN32_WINDOWS 0x0400
  #define _WIN32_WINNT 0x0400
  #define _WIN32_IE 0x0400

  #define UNICODE

  #ifdef __GNUC__
    #define DEPRECATED __attribute__((deprecated))
    #define NORETURN __attribute__((noreturn))
    #define UNUSED __attribute__((unused))
  #else
    #define DEPRECATED
    #define NORETURN
    #define UNUSED
  #endif

#endif

