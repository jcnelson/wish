#include "string_packet.h"

// initialize a wish_string_packets
int wish_init_string_packet( struct wish_state* state, struct wish_string_packet* wsp, char which, char const* str ) {
   memset( wsp, 0, sizeof(struct wish_string_packet) );
   wsp->which = which;
   if( str )
      wsp->str = strdup( str );
   else
      wsp->str = strdup( "" );
   return 0;
}

// initialize a wish_strings_packet
int wish_init_strings_packet( struct wish_state* state, struct wish_strings_packet* wssp, int count ) {
   memset( wssp, 0, sizeof(struct wish_strings_packet) );
   wssp->count = 0;
   wssp->packets = (struct wish_string_packet*)calloc( sizeof(struct wish_string_packet) * count, 1 );
   return 0;
}

// free a wish_string_packet's memory
int wish_free_string_packet( struct wish_string_packet* wsp ) {
   if( wsp->str ) {
      free( wsp->str );
      wsp->str = NULL;
   }
   return 0;  
}

// free a wish_strings_packet's memory
int wish_free_strings_packet( struct wish_strings_packet* wssp ) {
   if( wssp->packets ) {
      for( int i = 0; i < wssp->count; i++ ) {
         wish_free_string_packet( &wssp->packets[i] );
      }
      free( wssp->packets );
      wssp->packets = NULL;
   }
   return 0;
}

// calculate total number of bytes needed to pack a wish_string_packet
static int wish_string_packet_size( struct wish_string_packet* wsp ) {
   return strlen(wsp->str) + 1 + sizeof(wsp->which);
}

// write a wish_string_packet to a buffer, updating the offset
static int wish_add_buf_string_packet( struct wish_state* state, uint8_t* buf, off_t* offset, struct wish_string_packet* wsp ) {
   wish_pack_char( buf, offset, wsp->which );
   wish_pack_string( buf, offset, wsp->str );
   return 0;
}

// read a wish_string_packet from a buffer, updating the offset
static int wish_read_buf_string_packet( struct wish_state* state, uint8_t* buf, off_t* offset, struct wish_string_packet* wsp ) {
   wsp->which = wish_unpack_char( buf, offset );
   wsp->str = wish_unpack_string( buf, offset );
   return 0;
}

// add a wish_string_packet to a wish_strings_packet
int wish_add_string_packet( struct wish_state* state, struct wish_strings_packet* wssp, struct wish_string_packet* wsp ) {
   wssp->packets[ wssp->count ].which = wsp->which;
   wssp->packets[ wssp->count ].str = strdup( wsp->str );
   wssp->count++;
   return 0;
}

// make a string packet
int wish_pack_string_packet( struct wish_state* state, struct wish_packet* wp, struct wish_string_packet* wsp ) {
   wish_init_header( state, &wp->hdr, PACKET_TYPE_STRING );
   
   size_t len = wish_string_packet_size( wsp );
   uint8_t* buf = (uint8_t*)calloc(len, 1 );
   
   off_t offset = 0;
   wish_add_buf_string_packet( state, buf, &offset, wsp );
   
   wish_init_packet_nocopy( wp, &wp->hdr, buf, len );
   return 0;
}

// make a packet from multiple string packets
int wish_pack_strings_packet( struct wish_state* state, struct wish_packet* wp, struct wish_strings_packet* wssp ) {
   wish_init_header( state, &wp->hdr, PACKET_TYPE_STRINGS );
   
   off_t offset = 0;
   size_t total_len = 0;
   for( int i = 0; i < wssp->count; i++ ) {
      total_len += wish_string_packet_size( &wssp->packets[i] );
   }
   
   total_len += sizeof( wssp->count );
   
   uint8_t* buf = (uint8_t*)calloc( total_len, 1 );
   
   wish_pack_int( buf, &offset, wssp->count );
   for( int i = 0; i < wssp->count; i++ ) {
      wish_add_buf_string_packet( state, buf, &offset, &wssp->packets[i] );
   }
   
   wish_init_packet_nocopy( wp, &wp->hdr, buf, total_len );
   return 0;
}

// parse a string packet
int wish_unpack_string_packet( struct wish_state* state, struct wish_packet* wp, struct wish_string_packet* wsp ) {
   off_t offset = 0;
   
   wish_read_buf_string_packet( state, wp->payload, &offset, wsp );
   
   return 0;
}


// parse a lot of string packets
int wish_unpack_strings_packet( struct wish_state* state, struct wish_packet* wp, struct wish_strings_packet* wssp ) {
   off_t offset = 0;
   
   int count = wish_unpack_int( wp->payload, &offset );
   wish_init_strings_packet( state, wssp, count );
   
   for( int i = 0; i < count; i++ ) {
      struct wish_string_packet pkt;
      wish_read_buf_string_packet( state, wp->payload, &offset, &pkt );
      wish_add_string_packet( state, wssp, &pkt );
      wish_free_string_packet( &pkt );
   }
   
   return 0;
}
