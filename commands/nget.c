
// nget.c

#include "nget.h"

void usage( char* argv0 ) {
   fprintf(stderr,
"Usage: %s [-r|-l|-d|-c|-n] [-h HOST[:PORT]] RANK\n\
Options:\n\
   -l             lowest latency\n\
   -r             highest free RAM\n\
   -d             highest free disk\n\
   -c             highest free CPU\n\
   -h HOST[:PORT] Access the daemon running on HOST[:PORT]\n\
   -n             Don't print a host; print the number of nodes.\n\
                  If this option is given, RANK is ignored\n\
   RANK           The rank the desired host must have (0 being the highest/best\n",
argv0 );
   exit(1);
}


int main( int argc, char** argv ) {
   int c;
   int opt = 0;
   uint64_t rank = 0;
   bool get_count = false;
   char* hostname = NULL;
   int portnum = -1;
   uint32_t props = 0;
   
   while((c = getopt(argc, argv, "h:lrdcn")) != -1) {
      switch( c ) {
         case 'h': {
            // is there a hostname given?
            hostname = strdup( optarg );
            char* tmp = strchr(hostname, ':');
            if( tmp != NULL ) {
               *tmp = 0;
               char* tmp2;
               portnum = strtol(tmp + 1, &tmp2, 10 );
               if( tmp2 == tmp + 1 ) {
                  free( hostname );
                  usage( argv[0] );
               }
            }
            break;
         }
         case 'n': {
            if( !props )
               get_count = true;
            else
               usage(argv[0]);
            break;
         }
         case 'r':
            if( !props )
               props = HEARTBEAT_PROP_RAM;
            else
               usage(argv[0]);
            break;
            
         case 'l':
            if( !props )
               props = HEARTBEAT_PROP_LATENCY;
            else
               usage(argv[0]);
            break;
               
         case 'c':
            if( !props )
               props = HEARTBEAT_PROP_CPU;
            else
               usage(argv[0]);
            break;
            
         case 'd': {
            if( !props )
               props = HEARTBEAT_PROP_DISK;
            else
               usage(argv[0]);
            break;
         }
         default: {
            usage( argv[0] );
         }
      }
   }
   
   if( !get_count ) {
      // rank must be given
      if( optind >= argc ) {
         fprintf(stderr, "No rank given\n");
         usage(argv[0]);
      }
      else {
         char* tmp;
         rank = (uint64_t)strtoll( argv[optind], &tmp, 10 );
         if( tmp == argv[optind] ) {
            fprintf(stderr, "Could not parse rank\n");
            usage(argv[0]);
         }
      }
   } 
   else {
      if( props ) {
         fprintf(stderr, "Option %c is exclusive with -n\n", opt );
         usage(argv[0]);
      }
      else {
         props = HEARTBEAT_PROP_COUNT;
      }
   }
   
   // no hostname given?  then check the environment variables
   if( hostname == NULL ) {
      hostname = getenv( WISH_ORIGIN_ENV );
      if( hostname == NULL ) {
         hostname = (char*)"localhost";
      }
      else {
         char* portnum_str = getenv( WISH_PORTNUM_ENV );
         if( portnum_str ) {
            char* tmp;
            long port_candidate = strtol(portnum_str, &tmp, 10 );
            if( tmp != portnum_str ) {
               portnum = port_candidate;
            }
         }
      }
   }
   
   // read the config file
   struct wish_conf conf;
   int rc = wish_read_conf( WISH_DEFAULT_CONFIG, &conf );
   if( rc != 0 ) {
      fprintf(stderr, "Config file %s could not be read\n", WISH_DEFAULT_CONFIG );
      exit(1);
   }
   
   // override conf's portnum
   if( portnum > 0 ) {
      conf.portnum = portnum;
   }
   
   // connect to daemon
   struct wish_connection con;
   rc = wish_connect( NULL, &con, hostname, conf.portnum );
   if( rc != 0 ) {
      // could not connect
      fprintf(stderr, "Could not connect to daemon on %s:%d\n", hostname, conf.portnum);
      exit(1);
   }
   
   struct wish_packet pkt;
   struct wish_nget_packet npkt;
   
   wish_init_nget_packet( NULL, &npkt, rank, props );
   wish_pack_nget_packet( NULL, &pkt, &npkt );
   
   rc = wish_write_packet( NULL, &con, &pkt );
   wish_free_packet( &pkt );
   if( rc != 0 ) {
      // could not write
      fprintf(stderr, "Could not write to daemon on %s:%d\n", hostname, conf.portnum );
      exit(1);
   }
   
   // get back the hostname
   rc = wish_read_packet( NULL, &con, &pkt );
   if( rc != 0 ) {
      // could not read
      fprintf(stderr, "Could not read from daemon on %s:%d\n", hostname, conf.portnum );
      exit(1);
   }
   
   // this should be a string packet
   if( pkt.hdr.type != PACKET_TYPE_STRING ) {
      // nope
      fprintf(stderr, "Received invalid packet from daemon, type = %d\n", pkt.hdr.type );
      exit(1);
   }
   
   struct wish_string_packet spkt;
   wish_unpack_string_packet( NULL, &pkt, &spkt );
   
   printf("%s\n", spkt.str);
   
   wish_free_packet( &pkt );
   
   wish_disconnect( NULL, &con );
   
   exit(0);
}
