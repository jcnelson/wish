#ifndef _BARRIER_PACKET_H_
#define _BARRIER_PACKET_H_

#include "libwish.h"

#define PACKET_TYPE_BARRIER 3456

struct barrier_packet {
   uint64_t gpid_self;        // will be set to 0 if this is an ACK.
   uint64_t timeout;
   uint64_t num_procs;        // if 0, then a barrier will be requested on all existing processes
   uint64_t* gpids;
};

// make a barrier packet
int wish_init_barrier_packet( struct wish_state* state, struct barrier_packet* b, uint64_t gpid_self, uint64_t timeout, uint64_t num_procs, uint64_t* gpids );

// pack a barrier packet
int wish_pack_barrier_packet( struct wish_state* state, struct wish_packet* p, struct barrier_packet* b );

// unpack a barrier packet
int wish_unpack_barrier_packet( struct wish_state* state, struct wish_packet* p, struct barrier_packet* b );

// compare two barrier packets (i.e. do they have the same gpids?)
// return 1 if they do; return 0 otherwise
int wish_barrier_equal( struct barrier_packet* bp1, struct barrier_packet* bp2 );

// free a barrier packet
int wish_free_barrier_packet( struct barrier_packet* b );

#endif