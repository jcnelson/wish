// test client

#include "libwish.h"

#define CONF_FILE "libwish.conf"

int main( int argc, char** argv ) {

   struct wish_state state;
   memset( &state, 0, sizeof(state) );
   
   // send argv[1] to a server
   if( argc != 2 ) {
      fprintf(stderr, "Usage: %s TEXT\n", argv[0] );
      exit(1);
   }
   
   // read config
   int rc = wish_read_conf( CONF_FILE, &state.conf );
   if( rc != 0 ) {
      errorf("wish_read_conf rc = %d\n", rc );
      exit(1);
   }
   
   // intialize state
   rc = wish_init( &state );
   if( rc != 0 ) {
      errorf("wish_init rc = %d\n", rc );
      exit(1);
   }
   
   // connect to the daemon
   struct wish_connection con;
   rc = wish_connect( &state, &con, "localhost", state.conf.portnum );
   if( rc < 0 ) {
      errorf("wish_connect: rc = %d\n", rc );
      exit(1);
   }
   
   // make the packet
   struct wish_packet wp;
   memset( &wp, 0, sizeof(wp) );
   
   printf("send '%s' to %s:%d\n", argv[1], "localhost", state.conf.portnum );
   
   // wish_pack_string_packet( &state, &wp, argv[1] );
   struct wish_job_packet job;
   wish_init_job_packet( &job, 1, NULL, 0, argv[1], 0 );
   wish_pack_job_packet( &state, &wp, &job );
   
   // send the packet
   rc = wish_write_packet( &state, &con, &wp );
   if( rc < 0 ) {
      errorf("wish_write_packet: rc = %d\n", rc );
      exit(1);
   }
   
   // wait for a reply
   struct wish_packet reply;
   rc = wish_read_packet( &state, &con, &reply );
   if( rc < 0 ) {
      errorf("wish_read_packet: rc = %d\n", rc );
      exit(1);
   }
   
   // unmarshal the reply
   if( reply.hdr.type == PACKET_TYPE_STRING ) {
      char* str = NULL;
      wish_unpack_string_packet( &state, &reply, &str );
      printf("Got reply: '%s'\n", str );
      free( str );
   }
   else {
      printf("UNKNOWN REPLY %d\n", reply.hdr.type );
   }
   
   // close the connection
   wish_disconnect( &state, &con );
   
   // free memory
   wish_free_packet( &wp );
   wish_free_packet( &reply );
   
   // shutdown
   wish_shutdown( &state );
   
   return 0;
}
