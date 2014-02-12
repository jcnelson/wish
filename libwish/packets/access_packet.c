#include "access_packet.h"

void wish_init_access_packet( struct wish_state* state, struct access_packet* ap, int type, int num_paths, char** paths ) {
   memset( ap, 0, sizeof(struct access_packet) );
   
   ap->type = type;
   wish_init_strings_packet( state, &ap->list, num_paths );
   
   for( int i = 0; i < num_paths; i++ ) {
      struct wish_string_packet wsp;
      wish_init_string_packet( state, &wsp, 0, paths[i] );
      wish_add_string_packet( state, &ap->list, &wsp );
      wish_free_string_packet( &wsp );
   }
}


void wish_pack_access_packet( struct wish_state* state, struct wish_packet* wp, struct access_packet* ap ) {
   wish_init_header( state, &wp->hdr, PACKET_TYPE_ACCESS );
   
   off_t offset = 0;
   
   struct wish_packet wsp;    // string packet's packed data;
   wish_pack_strings_packet( state, &wsp, &ap->list );
   
   size_t packet_len = wsp.hdr.payload_len + sizeof(int);
   
   uint8_t* packet_buf = (uint8_t*)calloc( packet_len, 1 );
   
   wish_pack_int( packet_buf, &offset, ap->type );
   memcpy( packet_buf + offset, wsp.payload, wsp.hdr.payload_len );
   
   wish_free_packet( &wsp );
   
   wish_init_packet_nocopy( wp, &wp->hdr, packet_buf, packet_len );
}

void wish_unpack_access_packet( struct wish_state* state, struct wish_packet* wp, struct access_packet* ap ) {
   off_t offset = 0;
   
   ap->type = wish_unpack_int( wp->payload, &offset );
   
   struct wish_packet wsp;
   memcpy( &wsp, wp, sizeof(struct wish_packet) );
   wsp.payload += offset;
   
   wish_unpack_strings_packet( state, &wsp, &ap->list );
}

void wish_free_access_packet( struct access_packet* ap ) {
   wish_free_strings_packet( &ap->list );
}
