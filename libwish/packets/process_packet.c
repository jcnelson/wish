#include "process_packet.h"


// initialize a process packet
void wish_init_process_packet( struct wish_state* state, struct wish_process_packet* p, uint32_t type, uint64_t gpid, uint32_t signal, uint32_t data ) {
   p->type = type;
   p->gpid = gpid;
   p->signal = signal;
   p->data = data;
}

// make a psig packet
void wish_init_process_packet_psig( struct wish_state* state, struct wish_process_packet* p, uint64_t gpid, uint32_t signal ) {
   wish_init_process_packet( state, p, PROCESS_TYPE_PSIG, gpid, signal, 0 );
}

// make a pjoin packet
void wish_init_process_packet_pjoin( struct wish_state* state, struct wish_process_packet* p, uint64_t gpid ) {
   wish_init_process_packet( state, p, PROCESS_TYPE_PJOIN, gpid, 0, 0 );
}

// pack a process packet
int wish_pack_process_packet( struct wish_state* state, struct wish_packet* wp, struct wish_process_packet* p ) {
   wish_init_header( state, &wp->hdr, PACKET_TYPE_PROCESS );
   
   size_t len = sizeof(p->type) + sizeof(p->gpid) + sizeof(p->data) + sizeof(p->signal);
   
   uint8_t* buf = (uint8_t*)calloc( len, 1 );
   
   off_t offset = 0;
   wish_pack_uint( buf, &offset, p->type );
   wish_pack_ulong( buf, &offset, p->gpid );
   wish_pack_uint( buf, &offset, p->signal );
   wish_pack_uint( buf, &offset, p->data );
   
   wish_init_packet_nocopy( wp, &wp->hdr, buf, len );
   
   return 0;
}

// unpack a process packet
int wish_unpack_process_packet( struct wish_state* state, struct wish_packet* wp, struct wish_process_packet* p ) {
   off_t offset = 0;
   p->type = wish_unpack_uint( wp->payload, &offset );
   p->gpid = wish_unpack_ulong( wp->payload, &offset );
   p->signal = wish_unpack_uint( wp->payload, &offset );
   p->data = wish_unpack_uint( wp->payload, &offset );
   
   return 0;
}


// free a process packet
int wish_free_process_packet( struct wish_process_packet* pkt ) {
   return 0;
}