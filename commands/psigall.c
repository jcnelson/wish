
// psigall.c

#include "psigall.h"

void usage(char* argv0) {
   fprintf(stderr, "Usage: %s [-h HOST[:PORT]] -SIG\n", argv0);
   exit(0);
}
 
int main(int argc, char *argv[]) {

   int sig;
   int c;
   int portnum = -1;
   char* hostname = NULL;
   
   while((c = getopt(argc, argv, "h:0123456789")) != -1) {
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
                  errorf("Invalid argument for -%c\n", c);
                  usage( argv[0] );
               }
            }
            break;
         }
         case '0':
         case '1':
         case '2':
         case '3':
         case '4':
         case '5':
         case '6':
         case '7':
         case '8':
         case '9': {
            int cnt = sscanf( argv[optind-1], "-%d", &sig );
            if( cnt != 1 ) {
               printf("could not parse '%s'\n", argv[optind]);
               usage(argv[0]);
            }
            break;
         }
         default: {
            errorf("Unknown option '%c'\n", c);
            usage(argv[0]);
            break;
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
   
   // read the config file
   struct wish_conf conf;
   int rc = wish_read_conf( WISH_DEFAULT_CONFIG, &conf );
   if( rc != 0 ) {
      fprintf(stderr, "Config file %s could not be read\n", WISH_DEFAULT_CONFIG );
      exit(1);
   }
   
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
   struct wish_process_packet wpp;
   
   wish_init_process_packet( NULL, &wpp, PROCESS_TYPE_PSIGALL, 0, sig, 0 );
   wish_pack_process_packet( NULL, &pkt, &wpp );
   
   rc = wish_write_packet( NULL, &con, &pkt );
   wish_free_packet( &pkt );
   if( rc != 0 ) {
      // could not write
      fprintf(stderr, "Could not send to daemon on %s:%d\n", hostname, conf.portnum);
      exit(1);
   }
   
   // get a response
   rc = wish_read_packet( NULL, &con, &pkt );
   if( rc != 0 ) {
      // could not read
      fprintf(stderr, "Could not read process status on %s:%d\n", hostname, conf.portnum);
      exit(1);
   }
   
   wish_unpack_process_packet( NULL, &pkt, &wpp );
   if( wpp.type == PROCESS_TYPE_ACK ) {
      // got signal rc (should be 0)
      rc = wpp.data;
      if( rc != 0 ) {
         fprintf(stderr, "Could not signal %d processes\n", rc);
      }
   }
   else {
      fprintf(stderr, "Received unknown/corrupt ACK type %d\n", wpp.type );
      rc = 1;
   }
   
   wish_disconnect( NULL, &con );
   
   wish_free_packet( &pkt );
   
   return rc;
}

