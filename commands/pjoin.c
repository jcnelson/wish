#include "pjoin.h"

void usage( char* argv0 ) {
   fprintf(stderr,
"Usage: %s [-n] [-h HOST[:PORT]] GPID\n",
   argv0);
   
   exit(1);
}


int main( int argc, char** argv ) {
   // parse options
   int c;
   uint64_t gpid = 0;
   int block = 1;
   char* hostname = NULL;
   int portnum = -1;
   
   if( argc == 1 )
      usage( argv[0] );
   
   while((c = getopt(argc, argv, "n")) != -1) {
      switch( c ) {
         case 'n': {
            block = 0;
            break;
         }
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
         default: {
            usage( argv[0] );
         }
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
   
   c = sscanf( argv[optind], "%lu", &gpid );
      
   if( c != 1 ) {
      usage(argv[0]);
   }
   
   // read the config file
   struct wish_conf conf;
   int rc = wish_read_conf( WISH_DEFAULT_CONFIG, &conf );
   if( rc != 0 ) {
      fprintf(stderr, "Config file %s could not be read\n", WISH_DEFAULT_CONFIG );
      exit(1);
   }
   
   if( portnum <= 0 )
      portnum = conf.portnum;

   
   // connect to daemon
   struct wish_connection con;
   rc = wish_connect( NULL, &con, hostname, portnum );
   if( rc != 0 ) {
      // could not connect
      fprintf(stderr, "Could not connect to daemon on %s:%d\n", hostname, portnum);
      exit(1);
   }
   
   // force infinite timeout
   struct timeval tv;
   tv.tv_sec = 0;
   tv.tv_usec = 0;
   
   rc = setsockopt( con.soc, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv) );
   if( rc != 0 ) {
      fprintf(stderr, "setsockopt errno = %d\n", -errno );
      exit(1);
   }
   
   struct wish_packet pkt;
   struct wish_process_packet wpp;
   
   wish_init_process_packet( NULL, &wpp, PROCESS_TYPE_PJOIN, gpid, 0, block );
   wish_pack_process_packet( NULL, &pkt, &wpp );
   
   rc = wish_write_packet( NULL, &con, &pkt );
   wish_free_packet( &pkt );
   if( rc != 0 ) {
      // could not write
      fprintf(stderr, "Could not send to daemon on %s:%d\n", hostname, portnum);
      exit(1);
   }
   
   // get a response
   rc = wish_read_packet( NULL, &con, &pkt );
   if( rc != 0 ) {
      // could not read
      fprintf(stderr, "Could not read process status on %s:%d\n", hostname, portnum);
      exit(1);
   }
   
   wish_unpack_process_packet( NULL, &pkt, &wpp );
   if( wpp.type == PROCESS_TYPE_EXIT ) {
      // successfully joined!
      printf("%u\n", wpp.data);
   }
   else if( wpp.type == PROCESS_TYPE_ERROR ) {
      // non-blocking and -EAGAIN?
      if( !block && wpp.data == (uint32_t)(-EAGAIN) ) {
         // process is still running
         rc = EAGAIN;
      }
      else {
         fprintf(stderr, "Could not join with process %ld: rc = %d\n", gpid, wpp.data );
         exit(1);
      }
   }
   
   wish_disconnect( NULL, &con );
   
   wish_free_packet( &pkt );
   
   return rc;
}
