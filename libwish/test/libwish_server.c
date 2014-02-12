// test server

#include "libwish.h"

#define CONF_FILE "libwish.conf"

int main( int argc, char** argv ) {
   struct wish_state state;
   
   memset( &state, 0, sizeof(state) );
   
   // read config
   int rc = wish_read_conf( CONF_FILE, &state.conf );
   if( rc < 0 ) {
      errorf("wish_read_conf rc = %d\n", rc );
      exit(1);
   }
   
   // intialize state
   rc = wish_init( &state );
   if( rc < 0 ) {
      errorf("wish_init rc = %d\n", rc );
      exit(1);
   }
   
   // bind on an address
   rc = wish_init_daemon( &state );
   if( rc < 0 ) {
      errorf("wish_init_daemon rc = %d\n", rc );
      exit(1);
   }
   
   // accept connections
   while( 1 ) {
      struct wish_connection con;
      rc = wish_accept( &state, &con );
      if( rc < 0 ) {
         errorf("wish_accept rc = %d\n", rc );
         break;
      }
      
      // read the packet
      struct wish_packet packet;
      rc = wish_read_packet( &state, &con, &packet );
      if( rc < 0 ) {
         errorf("wish_read_packet rc = %d\n", rc );
         break;
      }
      
      // unmarshall the packet
      if( packet.hdr.type == PACKET_TYPE_STRING ) {
         char* str = NULL;
         wish_unpack_string_packet( &state, &packet, &str );
         printf("Got a string packet: '%s'\n", str );
         free( str );
      }
      else if( packet.hdr.type == PACKET_TYPE_JOB ) {
         struct wish_job_packet job;
         wish_unpack_job_packet( &state, &packet, &job );
         printf("Got a job packet: ttl = %d, visited_len = %d, cmd = '%s'\n", job.ttl, job.visited_len, job.cmd_text );
         
         wish_free_job_packet( &job );
      }
      else {
         printf("UNKNOWN PACKET TYPE %d\n", packet.hdr.type );
      }
      
      // free the packet
      wish_free_packet( &packet );
      
      // send a reply
      struct wish_packet reply;
      wish_pack_string_packet( &state, &reply, "OK" );
      rc = wish_write_packet( &state, &con, &reply );
      if( rc < 0 ) {
         errorf("wish_write_packet rc = %d\n", rc );
         break;
      }
      
      // close the connection
      rc = wish_disconnect( &state, &con );
      if( rc < 0 ) {
         errorf("wish_disconnect rc = %d\n", rc );
         break;
      }
   }
   
   wish_shutdown( &state );
   return 0;
}
