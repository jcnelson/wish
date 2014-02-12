// packet for sending messages about processes (exit status, signals, etc.)
// use wish_job_packet for sending actual process jobs.

#ifndef _PROCESS_PACKET_H_
#define _PROCESS_PACKET_H_

#include "libwish.h"

#define PACKET_TYPE_PROCESS 345

#define PROCESS_TYPE_PJOIN    0x1
#define PROCESS_TYPE_PSIG     0x2
#define PROCESS_TYPE_EXIT     0x3
#define PROCESS_TYPE_FAILURE  0x4
#define PROCESS_TYPE_STARTED  0x5
#define PROCESS_TYPE_SUCCESS  0x6
#define PROCESS_TYPE_ERROR    0x7
#define PROCESS_TYPE_TIMEOUT  0x8
#define PROCESS_TYPE_PSIGALL  0x9
#define PROCESS_TYPE_ACK      0xA
#define PROCESS_TYPE_GET_GPID 0xB      // wish_process_packet.data is the local pid to look up

// it is IMPERATIVE that this fits into a single TCP segment!
struct wish_process_packet {
   uint32_t type;             // pjoin, psig, exit, etc
   uint64_t gpid;             // (global) PID
   uint32_t data;             // data associated with the type
   uint32_t signal;           // signal associated with this process
};


// initialize a process packet
void wish_init_process_packet( struct wish_state* state, struct wish_process_packet* p, uint32_t type, uint64_t gpid, uint32_t signal, uint32_t data );

// make a psig packet
void wish_init_process_packet_psig( struct wish_state* state, struct wish_process_packet* p, uint64_t gpid, uint32_t signal );

// make a pjoin packet
void wish_init_process_packet_pjoin( struct wish_state* state, struct wish_process_packet* p, uint64_t gpid );

// pack a process packet
int wish_pack_process_packet( struct wish_state* state, struct wish_packet* wp, struct wish_process_packet* p );

// unpack a process packet
int wish_unpack_process_packet( struct wish_state* state, struct wish_packet* wp, struct wish_process_packet* p );

// free a process packet
int wish_free_process_packet( struct wish_process_packet* pkt );

#endif