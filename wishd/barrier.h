#ifndef _BARRIER_H_
#define _BARRIER_H_

#include "libwish.h"

using namespace std;

typedef pair<uint64_t, struct wish_connection*> Rendezvous;
typedef vector< Rendezvous > RendezvousList;


struct wish_barrier_status {
   struct barrier_packet* b_info;               // number and list of gpids of processes in this barrier
   uint64_t expire;                             // when does this barrier expire
   RendezvousList* proc_cons;                   // connections to running processes that have responded
};




int barrier_init( struct wish_state* state );

int barrier_shutdown( struct wish_state* state );

int barrier_add( struct wish_state* state, struct wish_connection* con, struct barrier_packet* bpkt );

int barrier_release( struct wish_state* state, struct wish_barrier_status* status );

int barrier_process( struct wish_state* state, struct wish_connection* con, struct barrier_packet* bpkt );

int barrier_purge_old( struct wish_state* state );

#endif