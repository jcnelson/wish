#include "barrier_packet.h"

// make a barrier packet
int wish_init_barrier_packet( struct wish_state* state, struct barrier_packet* b, uint64_t gpid_self, uint64_t timeout, uint64_t num_procs, uint64_t* gpids ) {
   memset( b, 0, sizeof(struct barrier_packet) );
   b->gpid_self = gpid_self;
   b->num_procs = num_procs;
   b->timeout = timeout;
   if( num_procs > 0 && gpids ) {
      b->gpids = (uint64_t*)calloc( sizeof(uint64_t) * num_procs, 1 );
      memcpy( b->gpids, gpids, sizeof(uint64_t) * num_procs );
   }
   else {
      b->gpids = NULL;
   }
   
   return 0;
}

// pack a barrier packet
int wish_pack_barrier_packet( struct wish_state* state, struct wish_packet* wp, struct barrier_packet* b ) {
   wish_init_header( state, &wp->hdr, PACKET_TYPE_BARRIER );
   
   size_t packet_len = sizeof(uint64_t) * 3 + sizeof(uint64_t) * b->num_procs;
   uint8_t* packet_buf = (uint8_t*)calloc( packet_len, 1 );
   
   off_t offset = 0;
   
   wish_pack_ulong( packet_buf, &offset, b->gpid_self );
   wish_pack_ulong( packet_buf, &offset, b->timeout );
   wish_pack_ulong( packet_buf, &offset, b->num_procs );
   for( uint64_t i = 0; i < b->num_procs; i++ ) {
      wish_pack_ulong( packet_buf, &offset, b->gpids[i] );
   }
   
   wish_init_packet_nocopy( wp, &wp->hdr, packet_buf, packet_len );
   
   return 0;
}

// unpack a barrier packet
int wish_unpack_barrier_packet( struct wish_state* state, struct wish_packet* wp, struct barrier_packet* b ) {
   off_t offset = 0;
   
   b->gpid_self = wish_unpack_ulong( wp->payload, &offset );
   b->timeout = wish_unpack_ulong( wp->payload, &offset );
   b->num_procs = wish_unpack_ulong( wp->payload, &offset );
   b->gpids = (uint64_t*)calloc( sizeof(uint64_t) * b->num_procs, 1 );
   
   for( uint64_t i = 0; i < b->num_procs; i++ ) {
      b->gpids[i] = wish_unpack_ulong( wp->payload, &offset );
   }
   
   return 0;
}

// free a barrier packet
int wish_free_barrier_packet( struct barrier_packet* b ) {
   if( b->gpids ) {
      free( b->gpids );
      b->gpids = NULL;
   }
   b->num_procs = 0;
   return 0;
}

// are two barrier packets equal?  do they refer to the same barrier?
int wish_barrier_equal( struct barrier_packet* bp1, struct barrier_packet* bp2 ) {
   if( bp1->num_procs != bp2->num_procs )
      return 0;
   
   uint64_t contains = 0;
   for( uint64_t i = 0; i < bp1->num_procs; i++ ) {
      for( uint64_t j = 0; j < bp2->num_procs; j++ ) {
         if( bp1->gpids[i] == bp2->gpids[j] ) {
            contains++;
         }
      }
   }
   
   if( contains == bp1->num_procs )
      return 1;
   else
      return 0;
}
