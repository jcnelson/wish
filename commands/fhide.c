
// fhide.c


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libwish.h"

void usage(char* argv0) {
    fprintf(stderr, "Usage: %s [-h HOST[:PORT]] PATH [PATH...]\n", argv0);
    exit(1);
}
 
int main(int argc, char *argv[]) {
   int rc = 0;
   int portnum = -1;
   int c;
   char* hostname = NULL;
   
   while((c = getopt(argc, argv, "h:")) != -1) {
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
   
   if( optind == argc ) {
      usage(argv[0]);
   }
   
   // read the config file
   struct wish_conf conf;
   rc = wish_read_conf( WISH_DEFAULT_CONFIG, &conf );
   if( rc != 0 ) {
      fprintf(stderr, "Config file %s could not be read\n", WISH_DEFAULT_CONFIG );
      exit(1);
   }
   
   // set portnum
   if( conf.portnum > 0 && portnum < 0 )
      portnum = conf.portnum;
   
   
   // connect to daemon
   struct wish_connection con;
   rc = wish_connect( NULL, &con, hostname, portnum );
   if( rc != 0 ) {
      // could not connect
      fprintf(stderr, "Could not connect to daemon on %s:%d, rc = %d\n", hostname, portnum, rc);
      exit(1);
   }
   
   struct wish_packet wp;
   struct access_packet ap;
   wish_init_access_packet( NULL, &ap, ACCESS_PACKET_TYPE_FHIDE, argc - optind, argv + optind );
   wish_pack_access_packet( NULL, &wp, &ap );
   
   rc = wish_write_packet( NULL, &con, &wp );
   wish_free_packet( &wp );
   
   if( rc != 0 ) {
      fprintf(stderr, "Could not send to daemon on %s:%d, rc = %d\n", hostname, portnum, rc );
      exit(1);
   }
   
   exit(0);
}

