#ifndef _HEARTBEAT_PACKET_H_
#define _HEARTBEAT_PACKET_H_

#include "libwish.h"

#define PACKET_TYPE_HEARTBEAT 234
#define PACKET_TYPE_NGET      2345

#define HEARTBEAT_PROP_NONE    0x0
#define HEARTBEAT_PROP_LATENCY 0x1
#define HEARTBEAT_PROP_CPU     0x2
#define HEARTBEAT_PROP_RAM     0x3
#define HEARTBEAT_PROP_DISK    0x4
#define HEARTBEAT_PROP_COUNT   0x5


struct wish_heartbeat_packet {
   uint32_t id;
   uint64_t loads[3];
   uint64_t ram_total;
   uint64_t ram_free;
   uint64_t disk_total;
   uint64_t disk_free;
   
   // not sent; used internally
   int64_t latency;              // latency, in microseconds
   struct timeval sendtime;      // send time
};

struct wish_nget_packet {
   uint64_t rank;
   uint32_t props;
};

// make a heartbeat packet, by reading the state of the system
int wish_init_heartbeat_packet( struct wish_state* state, struct wish_heartbeat_packet* h );
int wish_init_heartbeat_packet_ack( struct wish_state* state, struct wish_heartbeat_packet* ack, struct wish_heartbeat_packet* original );
int wish_init_nget_packet( struct wish_state* state, struct wish_nget_packet* npkt, uint64_t rank, uint32_t props );

// pack a heartbeat packet
int wish_pack_heartbeat_packet( struct wish_state* state, struct wish_packet* wp, struct wish_heartbeat_packet* h );
int wish_pack_nget_packet( struct wish_state* state, struct wish_packet* wp, struct wish_nget_packet* npkt );

// unpack a heartbeat packet
int wish_unpack_heartbeat_packet( struct wish_state* state, struct wish_packet* wp, struct wish_heartbeat_packet* h );
int wish_unpack_nget_packet( struct wish_state* state, struct wish_packet* wp, struct wish_nget_packet* npkt );

#endif