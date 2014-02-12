#include "wishd.h"

// global flag for running
static int g_running = 1;

static wish_state g_state;

// SIGINT/SIGQUIT/SIGTERM signal handlers--set running = false and repeat the signal
void quit_sigint( int param ) {
   g_running = 0;
   close( g_state.daemon_sock );
   //signal( SIGINT, SIG_DFL );
   //raise( SIGINT );
}

void quit_sigquit( int param ) {
   g_running = 0;
   close( g_state.daemon_sock );
   //signal( SIGQUIT, SIG_DFL );
   //raise( SIGQUIT );
}

void quit_sigterm( int param ) {
   g_running = 0;
   close( g_state.daemon_sock );
   //signal( SIGTERM, SIG_DFL );
   //raise( SIGTERM );
}

// packet processing loop
int wishd_main( struct wish_state* state ) {
   int rc = 0;
   
   // accept and process connections
   while( g_running ) {
      struct wish_connection* con = (struct wish_connection*)calloc( sizeof(struct wish_connection), 1 );
      rc = wish_accept( state, con );
      if( !g_running ) {
         free( con );
         return 0;
      }
      if( rc < 0 ) {
         errorf("wishd_main: wish_accept rc = %d\n", rc );
         free( con );
         continue;
      }
      
      printf("wish_connection: soc = %d, addr = %p, last_packet_recved = %p, have_header = %d\n", con->soc, con->addr, con->last_packet_recved, con->have_header );
      
      // read the packet
      struct wish_packet packet;
      
      rc = wish_read_packet( state, con, &packet );
      if( rc < 0 ) {
         errorf("wishd_main: wish_read_packet rc = %d\n", rc );
         free( con );
         continue;
      }
      
      // handle the packet
      switch( packet.hdr.type ) {
         
         case PACKET_TYPE_JOB: {
            // create a job and hand off the connection and job to the appropriate subsystem
            
            struct wish_job_packet* job = (struct wish_job_packet*)calloc( sizeof(struct wish_job_packet), 1 );
            wish_unpack_job_packet( state, &packet, job );
            printf("wishd_main: Got a job packet: dest nid = %lu, gpid = %lu, cmd = '%s', stdin = '%s', flags = %x\n", job->nid_dest, job->gpid, job->cmd_text, job->stdin_url, job->flags );
            
            // is this a wish-created job request?
            if( job->flags & JOB_WISH_ORIGIN ) {
               // process this job here
               dbprintf("starting %lu here...\n", job->gpid);
               int rc = process_start( state, con, job );
               if( rc != 0 ) {
                  errorf("wishd_main: process_start rc = %d\n", rc );
                  wish_process_reply( state, con, PROCESS_TYPE_ERROR, job->gpid, rc );
                  wish_disconnect( state, con );
                  free( con );
               }
               else {
                  dbprintf("started %lu\n", job->gpid);
               }
            }
            // this is a client-created job request
            else {
               dbprintf("spawning %lu...\n", job->gpid );
               int rc = process_spawn( state, job, con, job->nid_dest );
               if( rc != 0 ) {
                  errorf("wishd_main: process_spawn rc = %d\n", rc );
                  wish_process_reply( state, con, PROCESS_TYPE_ERROR, job->gpid, rc );
               }
               else {
                  dbprintf("spawned %lu\n", job->gpid);
               }
               wish_free_job_packet( job );
               free( job );
            }
            
            break;
         }
         
         case PACKET_TYPE_PROCESS: {
            struct wish_process_packet wpp;
            wish_unpack_process_packet( state, &packet, &wpp );
            printf("wishd_main: Process request, type = %d, gpid = %lu, data = %d\n", wpp.type, wpp.gpid, wpp.data );
            
            if( wpp.type == PROCESS_TYPE_PJOIN ) {
               // join request
               int blocking = wpp.data;
               int rc = process_join( state, con, wpp.gpid, (blocking != 0 ? true : false));
               if( rc != 0 ) {
                  errorf("wishd_main: process_join rc = %d\n", rc );
                  wish_process_reply( state, con, PROCESS_TYPE_ERROR, wpp.gpid, rc );
                  wish_disconnect( state, con );
                  free( con );
               }
            }
            else if( wpp.type == PROCESS_TYPE_PSIG ) {
               // signal
               int rc = process_send_signal( state, wpp.gpid, wpp.signal );
               if( rc != 0 ) {
                  errorf("wishd_main: process_send_signal rc = %d\n", rc );
               }
               
               rc = wish_process_reply( state, con, PROCESS_TYPE_ACK, wpp.gpid, rc );
               
               if( rc != 0 ) {
                  errorf("wishd_main: reply ACK to signal rc = %d\n", rc);
               }
               
               wish_disconnect( state, con );
               free( con );
            }
            else if( wpp.type == PROCESS_TYPE_PSIGALL ) {
               // signal all
               int rc = process_send_signal_all( state, wpp.signal );
               if( rc != 0 ) {
                  errorf("wishd_main: process_send_signal failed for %d process(es)\n", rc );
               }
               rc = wish_process_reply( state, con, PROCESS_TYPE_ACK, 0, rc );
               
               if( rc != 0 ) {
                  errorf("wishd_main: reply ACK to signal rc = %d\n", rc);
               }
               
               wish_disconnect( state, con );
               free( con );
            }
            else if( wpp.type == PROCESS_TYPE_GET_GPID ) {
               // get the GPID of a local process
               uint64_t gpid = process_get_gpid( state, wpp.data );
               if( gpid == 0 ) {
                  errorf("wishd_main: no such local process %d\n", wpp.data );
               }
               rc = wish_process_reply( state, con, PROCESS_TYPE_ACK, gpid, 0 );
               
               if( rc == 0 ) {
                  errorf("wishd_main: reply ACK to gpid lookup rc = %d\n", rc );
               }
               
               wish_disconnect( state, con );
               free( con );
            }
            else {
               errorf("wishd_main: cannot handle process packet of type %d\n", wpp.type );
               wish_disconnect( state, con );
               free( con );
            }
            break;
         }
         
         case PACKET_TYPE_HEARTBEAT: {
            rc = heartbeat_add( state, con, &packet );
            if( rc != 0 ) {
               errorf("wishd_main: heartbeat_add rc = %d\n", rc );
            }
            break;
         }
         
         case PACKET_TYPE_NGET: {
            struct wish_nget_packet nget_pkt;
            wish_unpack_nget_packet( state, &packet, &nget_pkt );
            dbprintf("nget request for the %lu(st/nd/rd/th) best node with props = %d\n", nget_pkt.rank, nget_pkt.props);
            
            struct wish_string_packet wsp;
            
            // get the number of known nodes?
            if( nget_pkt.props == HEARTBEAT_PROP_COUNT ) {
               char buf[100];
               sprintf(buf, "%lu", heartbeat_count_hosts( state ) + 1 );
               wish_init_string_packet( state, &wsp, STRING_STDOUT, buf );
            }
            else {
               uint64_t best_node = 0;
               unsigned int rank = nget_pkt.rank - 1;
               
               if( rank >= 0 && rank <= heartbeat_count_hosts( state ) ) {
                  if( nget_pkt.props == HEARTBEAT_PROP_LATENCY )
                     best_node = heartbeat_best_latency( state, rank );
                  else if( nget_pkt.props == HEARTBEAT_PROP_CPU )
                     best_node = heartbeat_best_cpu( state, rank );
                  else if( nget_pkt.props == HEARTBEAT_PROP_DISK )
                     best_node = heartbeat_best_disk( state, rank );
                  else if( nget_pkt.props == HEARTBEAT_PROP_RAM )
                     best_node = heartbeat_best_ram( state, rank );
                  else
                     best_node = heartbeat_index( state, rank );
               }
               
               if( best_node != 0 ) {
                  char* hostname = heartbeat_nid_to_hostname( state, best_node );
                  wish_init_string_packet( state, &wsp, STRING_STDOUT, hostname );
                  dbprintf("best_node = %lu, hostname = '%s'\n", best_node, hostname );
                  free( hostname );
               }
               else {
                  wish_init_string_packet( state, &wsp, STRING_STDOUT, "NONE" );
               }
            }
            
            struct wish_packet pkt;
            wish_pack_string_packet( state, &pkt, &wsp );
            rc = wish_write_packet( state, con, &pkt );
            wish_free_packet( &pkt );
            wish_free_string_packet( &wsp );
            
            if( rc != 0 ) {
               errorf("wishd_main: reply to nget rc = %d\n", rc);
            }
            
            wish_disconnect( state, con );
            free( con );
            break;
         }
         
         case PACKET_TYPE_BARRIER: {
            // got a barrier connection
            struct barrier_packet *bpkt = (struct barrier_packet*)calloc( sizeof(struct barrier_packet), 1 );
            wish_unpack_barrier_packet( state, &packet, bpkt );
            dbprintf("barrier request from %lu\n", bpkt->gpid_self);
            
            // process the barrier
            rc = barrier_process( state, con, bpkt );
            if( rc != 0 ) {
               errorf("wishd_main: barrier_process rc = %d\n", rc );
            }
            break;
         }
         
         
         case PACKET_TYPE_ACCESS: {
            // got an access packet
            struct access_packet ap;
            wish_unpack_access_packet( state, &packet, &ap );
            dbprintf("access request, type %d\n", ap.type );
            
            // fhide?
            if( ap.type == ACCESS_PACKET_TYPE_FHIDE ) {
               // blacklist all paths
               wish_state_wlock( state );
               for( int i = 0; i < ap.list.count; i++ ) {
                  dbprintf("fhide %s\n", ap.list.packets[i].str );
                  state->fs_invisible->push_back( strdup(ap.list.packets[i].str) );
               }
               wish_state_unlock( state );
            }
            // fshow?
            else if( ap.type == ACCESS_PACKET_TYPE_FSHOW ) {
               // un-blacklist all paths
               wish_state_wlock( state );
               
               for( int i = 0; i < ap.list.count; i++ ) {
                  dbprintf("fshow %s\n", ap.list.packets[i].str );
                  
                  if( state->fs_invisible->size() > 0 ) { 
                     for( vector<char*>::iterator itr = state->fs_invisible->begin() + (state->fs_invisible->size() - 1); itr != state->fs_invisible->begin(); itr-- ) {
                        if( strcmp( *itr, ap.list.packets[i].str ) == 0 ) {
                           free( *itr );
                           state->fs_invisible->erase( itr );
                        }
                     }
                  }
               }
               
               wish_state_unlock( state );
            }
            else {
               // unknown command
               errorf("wishd_main: unknown access request %d\n", ap.type );
            }
            
            break;
         }
         
         case PACKET_TYPE_CLIENT: {
            // got a client connection
            printf("Client connection request, socket %d\n", con->soc);
            wish_state_wlock( state );
            
            // insert over a NULL, or append if none found
            bool found = false;
            for( unsigned int i = 0; i < state->client_cons->size(); i++ ) {
               if( state->client_cons->at(i) == NULL ) {
                  (*state->client_cons)[i] = con;
                  found = true;
                  break;
               }
            }
            if( !found ) {
               state->client_cons->push_back( con );
            }
            wish_state_unlock( state );
            break;
         }
         
         default: {
            printf("wishd_main: UNKNOWN PACKET TYPE %d\n", packet.hdr.type );
            wish_connection_free( state, con );
            free( con );
            break;
         }
         
      }
      
      // free the packet
      wish_free_packet( &packet );
      
   }
   
   return rc;
}

// make an HTTP response for a file, if it exists
static int make_HTTP_file_response( struct HTTP_response** resp, char* path ) {
   struct stat sb;
   int rc = stat( path, &sb );
   if( rc == 0 ) {
      // woot!
      FILE* fh = fopen( path, "r" );
      if( !fh ) {
         // problem...
         rc = -errno;
      }
      else {
         *resp = (struct HTTP_response*)calloc( sizeof(struct HTTP_response), 1 );
         wish_create_HTTP_response_disk( *resp, "application/octet-stream", 200, fh, 0, sb.st_size );
         rc = 0;
      }
   }
   
   return rc;
}


// make an HTTP text response
static int make_HTTP_text_response( struct HTTP_response** resp, int code, char const* text ) {
   *resp = (struct HTTP_response*)calloc( sizeof(struct HTTP_response), 1 );
   wish_create_HTTP_response_ram( *resp, "text/plain", code, (char*)text, strlen(text) );
   return 0;
}


// HTTP GET handler
static struct HTTP_response* HTTP_GET_handler( struct HTTP* http, struct HTTP_user_entry* uent, void* cls, char* url, char* version, struct HTTP_header** client_headers ) {
   struct wish_state* state = http->state;
   struct HTTP_response* response = NULL;
   
   // extract the requested path
   char* path = strdup( url + 1 );
   
   // request for an environment variable?
   if( strlen(path) > strlen(WISH_HTTP_GETENV) + 2 && strncmp( path, WISH_HTTP_GETENV, strlen(WISH_HTTP_GETENV) ) == 0 ) {
      char* envar_name = path + strlen(WISH_HTTP_GETENV) + 1;
      
      dbprintf("HTTP_GET_handler/GETENV: request for environment variable '%s'\n", envar_name );
      
      // look up the envar and reply it
      char* envar_value = envar_get( envar_name );
      if( envar_value == NULL ) {
         // not set?  reply the empty string
         make_HTTP_text_response( &response, 200, "" );
      }
      else {
         make_HTTP_text_response( &response, 200, envar_value );
         free( envar_value );
      }
   }
   
   // request to set an environment variable?
   else if( strlen(path) > strlen(WISH_HTTP_SETENV) + 3 && strncmp( path, WISH_HTTP_SETENV, strlen(WISH_HTTP_SETENV) ) == 0 ) {
      char* envar_assign = strdup( path + strlen(WISH_HTTP_SETENV) + 1 );
      
      // syntax of envar_assign: NAME=VALUE
      char* tmp = NULL;
      char* envar_name = strtok_r( envar_assign, " =", &tmp );
      if( envar_name ) {
         char* envar_value = strtok_r( NULL, "\0", &tmp );
         if( envar_value ) {
            dbprintf("HTTP_GET_handler/SETENV: set '%s' = '%s'\n", envar_name, envar_value );
            envar_set( envar_name, envar_value );
            make_HTTP_text_response( &response, 200, "200 OK" );
         }
         else {
            make_HTTP_text_response( &response, 400, "400 Bad Request" );
         }
      }
      else {
         make_HTTP_text_response( &response, 400, "400 Bad Request" );
      }
   }
   
   // request for a test-and-set?
   else if( strlen(path) > strlen(WISH_HTTP_TASET) + 5 && strncmp( path, WISH_HTTP_TASET, strlen(WISH_HTTP_TASET) ) == 0 ) {
      char* envar_assign = strdup( path + strlen(WISH_HTTP_TASET) + 1 );
      
      // syntax of envar_assign: NAME=VALUE,IF_EQUAL
      char* tmp = NULL;
      char* envar_name = strtok_r( envar_assign, " =", &tmp );
      if( envar_name ) {
         char* envar_value = strtok_r( NULL, ",", &tmp );
         if( envar_value ) {
            char* envar_cmp = strtok_r( NULL, "\0", &tmp );
            if( !envar_cmp )
               envar_cmp = (char*)"";
            
            dbprintf("HTTP_GET_handler/TASET: request to set '%s' = '%s' if '%s' == '%s'\n", envar_name, envar_value, envar_name, envar_cmp );
            
            char* curr_value = envar_get( envar_name );
            if( !curr_value )
               curr_value = strdup( "" );
            
            if( strcmp( curr_value, envar_cmp ) == 0 ) {
               // equal--reply the new value
               make_HTTP_text_response( &response, 200, envar_cmp );
               envar_set( envar_name, envar_value );
               dbprintf("HTTP_GET_handler/TASET: set %s = %s\n", envar_name, envar_value );
            }
            else {
               // not equal
               make_HTTP_text_response( &response, 400, "400 Bad Request" );
            }
            
            free( curr_value );
         }
         else {
            make_HTTP_text_response( &response, 400, "400 Bad Request" );
         }
      }
      else {
         make_HTTP_text_response( &response, 400, "400 Bad Request" );
      }
      free( envar_assign );
   }
   
   // request for a temporary file?
   else if( strlen(path) > strlen(WISH_HTTP_TEMP) + 1 && strncmp( path, WISH_HTTP_TEMP, strlen(WISH_HTTP_TEMP) ) == 0 ) {
      char* temp_path = path + strlen(WISH_HTTP_TEMP);
      
      wish_state_rlock( state );
      char* request_path = fullpath( state->conf.tmp_dir, temp_path, NULL );
      wish_state_unlock( state );
      
      dbprintf("HTTP_GET_handler/TEMP: request for temporary file %s\n", request_path );
      
      // does the requested file exist?  Make a response for it
      int rc = make_HTTP_file_response( &response, request_path );
      if( rc != 0 ) {
         // could not read file
         make_HTTP_text_response( &response, 404, "404 File Not Found" );
      }
   }
   
   // request for a file?
   else if( strlen(path) > strlen(WISH_HTTP_FILE) + 1 && strncmp( path, WISH_HTTP_FILE, strlen(WISH_HTTP_FILE) ) == 0 ) {
      
      char* file_path = path + strlen(WISH_HTTP_FILE);
      
      wish_state_rlock( state );
      char* request_path = fullpath( state->conf.files_root, file_path, NULL );
      wish_state_unlock( state );
      
      dbprintf("HTTP_GET_handler/FILE: request for file %s\n", request_path);
      
      // make sure it's allowed
      bool blocked = false;
      for( vector<char*>::size_type i = 0; i < state->fs_invisible->size(); i++ ) {
         if( strncmp( request_path, state->fs_invisible->at(i), MIN( strlen(request_path), strlen(state->fs_invisible->at(i)) ) ) == 0 ) {
            // this directory is meant to be invisible...
            errorf("HTTP_GET_handler: ignore request for %s, which is invisible\n", url );
            blocked = true;
            break;
         }
      }
      
      if( !blocked ) {
         // can read
         int rc = make_HTTP_file_response( &response, request_path );
         if( rc != 0 ) {
            // problem reading
            make_HTTP_text_response( &response, 404, "404 File Not Found" );
         }
      }
      else {
         make_HTTP_text_response( &response, 403, "403 Forbidden" );
      }
      
      free( request_path );
   }
   else {
      make_HTTP_text_response( &response, 400, "400 Bad Request" );
   }
   
   free( path );
   
   return response;
}


void usage( char* argv0 ) {
   fprintf(stderr,
"\
Usage: %s [-c CONFIG] \n\
Options:\n\
   -c CONFIG               Use config file at CONFIG \n\
\n",
   argv0 );
   
   exit(1);
}


int main( int argc, char** argv ) {
   
   memset( &g_state, 0, sizeof(g_state) );
   
   // parse options
   int c;
   char* config_path = NULL;
   
   while((c = getopt(argc, argv, "c:")) != -1) {
      switch( c ) {
         case 'c': {
            config_path = optarg;
            break;
         }
         default: {
            usage( argv[0] );
         }
      }
   }
   
   if( config_path == NULL )
      config_path = (char*)DEFAULT_CONFIG_PATH;
   
   // read config
   int rc = wish_read_conf( config_path, &g_state.conf );
   if( rc < 0 ) {
      errorf("main: wish_read_conf rc = %d\n", rc );
      exit(1);
   }
   
   // intialize state
   rc = wish_init( &g_state );
   if( rc < 0 ) {
      errorf("main: wish_init rc = %d\n", rc );
      exit(1);
   }
   
   printf("My NID is %lu (hostname is %s)\n", g_state.nid, g_state.hostname );
   
   // bind on an address
   rc = wish_init_daemon( &g_state );
   if( rc < 0 ) {
      errorf("main: wish_init_daemon rc = %d\n", rc );
      exit(1);
   }
   
   // set temporary files environment variable, so all child processes will have
   // $WISH_TMPDIR and $WISH_DATADIR set
   rc = setenv(WISH_TMPDIR_ENV, g_state.conf.tmp_dir, 1);
   if( rc < 0 ) {
      errorf("main: setenv %s = %s rc = %d, errno = %d\n", WISH_TMPDIR_ENV, g_state.conf.tmp_dir, rc, -errno );
      exit(1);
   }
   
   rc = setenv(WISH_DATADIR_ENV, g_state.conf.files_root, 1 );
   if( rc < 0 ) {
      errorf("main: setenv %s = %s rc = %d, errno = %d\n", WISH_DATADIR_ENV, g_state.conf.files_root, rc, -errno );
      exit(1);
   }
   
   // set up heartbeats
   rc = heartbeat_init( &g_state );
   if( rc < 0 ) {
      errorf("main: heartbeat_init rc = %d\n", rc );
      exit(1);
   }
   
   // set up processes
   rc = process_init( &g_state );
   if( rc < 0 ) {
      errorf("main: process_init rc = %d\n", rc );
      exit(1);
   }
   
   // set up HTTP
   struct HTTP_user_entry** users = NULL;
   if( g_state.conf.http_secrets )
      users = wish_parse_secrets_file( g_state.conf.http_secrets );
   
   struct HTTP http;
   rc = wish_HTTP_init( &http, &g_state, users );
   if( rc < 0 ) {
      errorf("main: wish_HTTP_init rc = %d\n", rc );
      exit(1);
   }
   
   // set handlers
   http.HTTP_GET_handler = HTTP_GET_handler;
   
   // start HTTP
   rc = wish_start_HTTP( &http );
   if( rc < 0 ) {
      errorf("main: wish_start_HTTP rc = %d\n", rc );
      exit(1);
   }
   
   // set up signal handlers
   signal( SIGINT, quit_sigint );
   signal( SIGQUIT, quit_sigquit );
   signal( SIGTERM, quit_sigterm );
   
   // ignore SIGPIPE (broken pipe errors)--have the send() caller handle them
   signal( SIGPIPE, SIG_IGN);
   
   rc = wishd_main( &g_state );
   dbprintf("main: wishd_main returned %d\n", rc );
   
   rc = process_shutdown( &g_state );
   dbprintf("main: process shutdown rc = %d\n", rc );
   
   rc = wish_stop_HTTP( &http );
   dbprintf("main: HTTP shutdown rc = %d\n", rc );
   
   rc = heartbeat_shutdown( &g_state );
   dbprintf("main: heartbeat shutdown rc = %d\n", rc );
   
   rc = wish_shutdown( &g_state );
   dbprintf("main: wish shutdown rc = %d\n", rc );
   
   return rc;
}
