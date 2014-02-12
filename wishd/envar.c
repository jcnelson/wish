#include "envar.h"

static EnvarMap envars;
pthread_rwlock_t envars_lock;

static int envar_rlock(void) {
   return pthread_rwlock_rdlock( &envars_lock );
}

static int envar_wlock(void) {
   return pthread_rwlock_wrlock( &envars_lock );
}

static int envar_unlock(void) {
   return pthread_rwlock_unlock( &envars_lock );
}
      
// initialize envars
void envar_init(void) {
   pthread_rwlock_init( &envars_lock, NULL );
}

// shutdown envars
void envar_shutdown(void) {
   envar_wlock();
   for( EnvarMap::iterator itr = envars.begin(); itr != envars.end(); itr++ ) {
      free( itr->second );
   }
   envars.clear();
   envar_unlock();
   
   pthread_rwlock_destroy( &envars_lock );
}

// get an environment variable.
// the caller must free the value returned.
// returns NULL if not found
char* envar_get( char const* name ) {
   string sname = name;
   char* ret = NULL;
   
   envar_rlock();
   EnvarMap::iterator itr = envars.find( sname );
   if( itr != envars.end() ) {
      ret = strdup( itr->second );
   }
   envar_unlock();
   
   return ret;
}

// set an environment variable
void envar_set( char const* name, char const* value ) {
   string sname = name;
   
   envar_wlock();
   EnvarMap::iterator itr = envars.find( sname );
   if( itr != envars.end() ) {
      free( itr->second );
      itr->second = NULL;
   }
   envars[sname] = strdup( value );
   envar_unlock();
   
   return;
}

// atomically test and set an environment variable.
// return 0 on success; negative on error.
int envar_taset( char const* name, char const* cmp, char const* new_value ) {
   return -ENOSYS;
}
