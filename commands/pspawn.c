#include "pspawn.h"

void usage( char* argv0 ) {
   fprintf(stderr,
"Usage: %s [-d] [-t TIMEOUT] [-h HOST[:PORT]] [-g GPID] [-i STDIN] [-o STDOUT] [-e STDERR] [-f FILE] [-c COMMAND] HOST\n",
   argv0);
   
   exit(1);
}

int get_umask() {
   int ret = umask(0);
   umask(ret);
   return ret;
}


int main( int argc, char** argv ) {
   // parse options
   int c;
   uint64_t nid = 0;
   int portnum = -1;
   time_t timeout = -1;
   uint32_t flags = 0;
   char* file_path = NULL;
   char* stdin_path = NULL;
   char* stdout_path = NULL;
   char* stderr_path = NULL;
   char* cmd_str = NULL;
   char* hostname = NULL;
   uint64_t gpid = 0;
   
   while((c = getopt(argc, argv, "h:dt:f:g:c:i:o:e:")) != -1) {
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
            nid = wish_host_nid( hostname );
            break;
         }
         case 'd': {
            flags |= JOB_DETACHED;
            break;
         }
         case 'f' : {
            if( cmd_str )
               usage( argv[0] );
            
            file_path = realpath( optarg, NULL );
            flags |= JOB_USE_FILE;
            break;
         }
         case 't': {
            int cnt = sscanf( optarg, "%ld", &timeout );
            if( cnt != 1 )
               usage(argv[0]);
            break;
         }
         case 'g': {
            int cnt = sscanf( optarg, "%lu", &gpid );
            if( cnt != 1 )
               usage(argv[0]);
            
            break;
         }
         case 'c': {
            if( file_path )
               usage( argv[0] );
            
            cmd_str = optarg;
            break;
         }
         case 'i': {
            stdin_path = optarg;
            break;
         }
         case 'o': {
            stdout_path = optarg;
            break;
         }
         case 'e': {
            stderr_path = optarg;
            break;
         }
         default: {
            usage( argv[0] );
         }
      }
   }
   
   if( optind != argc - 1 ) {
      usage( argv[0] );
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
   
   if( cmd_str == NULL && file_path == NULL ) {
      usage( argv[0] );
   }
   
   nid = wish_host_nid( argv[argc - 1] );
   
   // read the config file
   struct wish_conf conf;
   int rc = wish_read_conf( WISH_DEFAULT_CONFIG, &conf );
   if( rc != 0 ) {
      fprintf(stderr, "Config file %s could not be read\n", WISH_DEFAULT_CONFIG );
      exit(1);
   }
   
   // set portnum
   if( conf.portnum > 0 && portnum < 0 )
      portnum = conf.portnum;
   
   // formulate the packet
   if( !file_path ) {
      cmd_str = cmd_str;
   }
   else {
      cmd_str = file_path;
   }
   
   // connect to daemon
   struct wish_connection con;
   rc = wish_connect( NULL, &con, hostname, portnum );
   if( rc != 0 ) {
      // could not connect
      fprintf(stderr, "Could not connect to daemon on %s:%d, rc = %d\n", hostname, portnum, rc);
      exit(1);
   }
   
   struct wish_packet pkt;
   struct wish_job_packet jpkt;
   
   wish_init_job_packet_client( NULL, &jpkt, gpid, nid, 1, cmd_str, stdin_path, stdout_path, stderr_path, getuid(), getgid(), get_umask(), flags, timeout );
   wish_pack_job_packet( NULL, &pkt, &jpkt );
   
   // send of the job request
   rc = wish_write_packet( NULL, &con, &pkt );
   if( rc != 0 ) {
      // could not write
      fprintf(stderr, "Could not send to daemon on %s:%d, rc = %d\n", hostname, portnum, rc);
      exit(1);
   }
   
   // wait for a reply that this job has started
   wish_free_packet( &pkt );
   rc = wish_read_packet( NULL, &con, &pkt );
   if( rc != 0 ) {
      // could not read
      fprintf(stderr, "Could not read reply from daemon on %s:%d, rc = %d\n", hostname, portnum, rc);
      exit(1);
   }
   
   if( pkt.hdr.type != PACKET_TYPE_PROCESS ) {
      // invalid packet
      fprintf(stderr, "Corrupt response from daemon on %s:%d, rc = %d\n", hostname, portnum, rc);
      exit(1);
   }
   
   struct wish_process_packet resp;
   wish_unpack_process_packet( NULL, &pkt, &resp );
   
   if( resp.type != PROCESS_TYPE_STARTED ) {
      fprintf(stderr, "Daemon relied code %d\n", resp.type );
      exit(1);
   }
   
   printf("%lu\n", jpkt.gpid );
   
   wish_disconnect( NULL, &con );
   
   wish_free_job_packet( &jpkt );
   wish_free_packet( &pkt );
   
   return 0;
}
