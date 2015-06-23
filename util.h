#ifndef    __UTIL_H__
#define    __UTIL_H__
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef DEBUG
 #define dbg(fmt, args...) do{ fprintf(stderr, "%s/%s(%d): " fmt, \
     __func__,__FILE__, __LINE__, ##args); }while(0)
#else
 #define dbg(fmt, args...) do{}while(0)/* Don't do anything in release builds */
#endif

#define handle_error(msg) do { perror(msg); exit(EXIT_FAILURE); }while(0)

char * zsys_sprintf (const char *format, ...);
char * zsys_vprintf (const char *format, va_list argptr);
int systemf(const char* format, ...);



#endif
