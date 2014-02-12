
// definition of a string packet

#ifndef _STRING_PACKET_H_
#define _STRING_PACKET_H_


#define PACKET_TYPE_STRING      0
#define PACKET_TYPE_STRINGS     1

#define STRING_STDOUT      0
#define STRING_STDERR      1

#include <sys/types.h>

struct wish_string_packet {
   char which;    // stdout or stderr?
   char* str;     // the text
};

struct wish_strings_packet {
   int32_t count;
   struct wish_string_packet* packets;
};


#include "libwish.h"

int wish_init_string_packet( struct wish_state* state, struct wish_string_packet* wsp, char which, char const* str );
int wish_free_string_packet( struct wish_string_packet* wsp );
int wish_free_strings_packet( struct wish_strings_packet* wssp );

int wish_init_strings_packet( struct wish_state* state, struct wish_strings_packet* wssp, int count );
int wish_add_string_packet( struct wish_state* state, struct wish_strings_packet* wssp, struct wish_string_packet* pkt );

// make a string packet
// return 0 on success; negative on error
int wish_pack_string_packet( struct wish_state* state, struct wish_packet* wp, struct wish_string_packet* wsp );
int wish_pack_strings_packet( struct wish_state* state, struct wish_packet* wp, struct wish_strings_packet* wssp );

// parse a string packet
// return 0 on success; negative on error
int wish_unpack_string_packet( struct wish_state* state, struct wish_packet* wp, struct wish_string_packet* wsp );
int wish_unpack_strings_packet( struct wish_state* state, struct wish_packet* wp, struct wish_strings_packet* wssp );

#endif
