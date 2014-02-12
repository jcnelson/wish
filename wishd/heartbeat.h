// code that maintains connections to other hosts in a WISH instance

#ifndef _HEARTBEAT_H_
#define _HEARTBEAT_H_

#include "libwish.h"
#include <map>
#include <string>
#include <locale>
#include <math.h>
#include <algorithm>

using namespace std;

struct wish_host_status {
   vector<struct wish_heartbeat_packet*>* pending;     // list of packets sent to this host that have not been acknowledged
   vector<struct wish_heartbeat_packet*>* heartbeats;  // list of the last heartbeat packets we've received from this host
   
   struct wish_connection con;                    // connection to this host
   char* hostname;                                // hostname of this host
   int portnum;                                   // portnum of this host (in case we need to repair the connection)
   
   uint64_t nid;                                  // node ID of this host
};

// initialize heartbeat monitoring
int heartbeat_init( struct wish_state* state );

// shut down heartbeat monitoring
int heartbeat_shutdown( struct wish_state* state );

// process an inbound heartbeat connection
int heartbeat_add( struct wish_state* state, struct wish_connection* con, struct wish_packet* wp );

// process an inbound heartbeat packet
int heartbeat_process( struct wish_state* state, struct wish_connection* con, struct wish_packet* wp );

// get a connection given a hostname.
// return error if not found.
// caller must free the connection (with wish_connection_free)
int heartbeat_get_hostname( struct wish_state* state, char const* hostname, struct wish_connection* con, uint64_t* nid );

// get a connection given a NID. 
// return error if not found.
// caller must free the connection (with wish_connection_free)
int heartbeat_get_nid( struct wish_state* state, uint64_t nid, struct wish_connection* con );

// convert a NID into a hostname
char* heartbeat_nid_to_hostname( struct wish_state* state, uint64_t nid );

// convert a NID into a portnum for a particular host
int heartbeat_nid_to_portnum( struct wish_state* state, uint64_t nid );

// how many hosts?
uint64_t heartbeat_count_hosts( struct wish_state* state );

uint64_t heartbeat_best_latency( struct wish_state* state, unsigned int best );
uint64_t heartbeat_best_cpu( struct wish_state* state, unsigned int best );
uint64_t heartbeat_best_ram( struct wish_state* state, unsigned int best );
uint64_t heartbeat_best_disk( struct wish_state* state, unsigned int best );
uint64_t heartbeat_index( struct wish_state* state, unsigned int best );

#endif