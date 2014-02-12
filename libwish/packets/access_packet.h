#ifndef _ACCESS_PACKET_H_
#define _ACCESS_PACKET_H_

#include "libwish.h"
#include "string_packet.h"

#define PACKET_TYPE_ACCESS 1357

#define ACCESS_PACKET_TYPE_FSHOW 1
#define ACCESS_PACKET_TYPE_FHIDE 2

struct access_packet {
   int type;
   struct wish_strings_packet list;
};

void wish_init_access_packet( struct wish_state* state, struct access_packet* ap, int type, int num_paths, char** paths );

void wish_pack_access_packet( struct wish_state* state, struct wish_packet* wp, struct access_packet* ap );

void wish_unpack_access_packet( struct wish_state* state, struct wish_packet* wp, struct access_packet* ap );

void wish_free_access_packet( struct access_packet* ap );

#endif