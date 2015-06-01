#include "util.h"
#include <stdlib.h>

char * zsys_vprintf (const char *format, va_list argptr)
{
    int size = 256;
    char *string = (char *) malloc (size);
    //  Using argptr is destructive, so we take a copy each time we need it
    //  We define va_copy for Windows in czmq_prelude.h
    va_list my_argptr;
    va_copy (my_argptr, argptr);
    int required = vsnprintf (string, size, format, my_argptr);
    va_end (my_argptr);
    //  If formatted string cannot fit into small string, reallocate a
    //  larger buffer for it.
    if (required >= size) {
        size = required + 1;
        free (string);
        string = (char *) malloc (size);
        if (string) {
            va_copy (my_argptr, argptr);
            vsnprintf (string, size, format, my_argptr);
            va_end (my_argptr);
        }
    }
    return string;
}
char * zsys_sprintf (const char *format, ...)
{
    va_list argptr;
    va_start (argptr, format);
    char *string = zsys_vprintf (format, argptr);
    va_end (argptr);
    return (string);
}
int systemf(const char* format, ...)
{
	va_list argptr;
	int r;
    va_start (argptr, format);
    char *string = zsys_vprintf (format, argptr);
    va_end (argptr);
    
    if(!string){
		dbg("system command not executed\n");
		return -1;
	}
	
	r= system(string);
	free(string);
    return r;
}
    

