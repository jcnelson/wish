// environment variable implementation
#ifndef _ENVAR_H_
#define _ENVAR_H_

#include "libwish.h"
#include <map>
#include <string>

using namespace std;

typedef map<string, char*> EnvarMap;

// initialize envars
void envar_init(void);

// shutdown envars
void envar_shutdown(void);

// get an environment variable.
// the caller must free the value returned.
// returns NULL if not found
char* envar_get( char const* name );

// set an environment variable
void envar_set( char const* name, char const* value );

// atomically test and set an environment variable.
// return 0 on success; negative on error.
int envar_taset( char const* name, char const* cmp, char const* new_value );

#endif