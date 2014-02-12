
// psync.c
// This program SHOULD NOT honor WISH_ORIGIN and WISH_PORTNUM

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libwish.h"

void usage(char* argv0) {

   fprintf(stderr, "Usage: %s [-h HOST[:PORT]] [-t TIMEOUT] GPID [GPID...]\n", argv0);
   exit(0);
}


int main(int argc, char *argv[]) {

   uint64_t* gpids = NULL;
   int rc = 0;
   int portnum = -1;
   int c;
   time_t timeout = 0;
   char* hostname = NULL;
   
   while((c = getopt(argc, argv, "h:t:")) != -1) {
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
         case 't': {
            int cnt = sscanf(optarg, "%lu", &timeout);
            if( cnt != 1 )
               usage(argv[0]);
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
   
   // what's our GPID?
   uint64_t my_gpid = 0;
   char* gpid_txt = getenv( WISH_GPID_ENV );
   if( gpid_txt == NULL ) {
      // no GPID set
      fprintf(stderr, "ERROR: this isn't a WISH-managed process!\n");
      usage(argv[0]);
   }
   
   rc = sscanf(gpid_txt, "%lu", &my_gpid );
   if( rc != 1 || my_gpid == 0 ) {
      // could not parse
      fprintf(stderr, "ERROR: could not determine this process's GPID\n");
      usage(argv[0]);
   }
   
   int num_gpids = 0;
   
   gpids = (uint64_t*)calloc( sizeof(uint64_t) * (argc - optind + 1), 1 );
   for( int i = optind; i < argc; i++ ) {
      uint64_t gpid = 0;
      int count = sscanf( argv[i], "%ld", &gpid );
      if( count != 1 ) {
         usage(argv[0]);
      }
      
      gpids[num_gpids] = gpid;
      num_gpids++;
   }
   
   // get our config
   struct wish_conf conf;
   rc = wish_read_conf( WISH_DEFAULT_CONFIG, &conf );
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
   struct barrier_packet bpkt;
   
   wish_init_barrier_packet( NULL, &bpkt, my_gpid, timeout, num_gpids, gpids );
   wish_pack_barrier_packet( NULL, &pkt, &bpkt );
   
   // send to daemon
   rc = wish_write_packet( NULL, &con, &pkt );
   wish_free_packet( &pkt );
   
   if( rc != 0 ) {
      // could not write
      fprintf(stderr, "Could not send to daemon on %s:%d\n", hostname, conf.portnum);
      exit(1);
   }
   
   // set timeout locally
   rc = wish_recv_timeout( NULL, &con, timeout * 1000 );
   if( rc != 0 ) {
      fprintf(stderr, "Could not set socket timeout\n");
      exit(1);
   }
   
   // wait for response
   errno = 0;
   rc = wish_read_packet( NULL, &con, &pkt );
   if( rc != 0 ) {
      // could not read
      fprintf(stderr, "Could not read barrier status on %s:%d\n", hostname, conf.portnum);
      exit(1);
   }
   
   // process the response--it should be a barrier packet
   if( pkt.hdr.type != PACKET_TYPE_BARRIER ) {
      fprintf(stderr, "Received invalid packet type %d from %s:%d\n", pkt.hdr.type, hostname, conf.portnum );
      exit(1);
   }
   else {
      struct barrier_packet reply;
      wish_unpack_barrier_packet( NULL, &pkt, &reply );
      if( !wish_barrier_equal( &reply, &bpkt ) ) {
         fprintf(stderr, "Received invalid barrier acknowledgement from %s:%d\n", hostname, conf.portnum );
         exit(1);
      }
   }
   
   // success!
   exit(0);
}

