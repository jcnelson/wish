#include "libwish.h"


int _DEBUG = 0;

// calculate a NID from a hostname
uint64_t wish_host_nid( char const* hostname ) {
   locale loc;
   const collate<char>& coll = use_facet<collate<char> >(loc);
   return (uint64_t)coll.hash( hostname, hostname + strlen(hostname) );
}

// read a host entry from a string (formatted as host:port)
static int wish_parse_hostent( char* line, struct wish_hostent* host ) {
   char* line_copy = strdup( line );
   
   char* hostname = strtok( line_copy, ":" );
   char* portnum_str = strtok( NULL, "\0" );
   
   host->hostname = strdup( hostname );
   host->portnum = strtol( portnum_str, NULL, 10 );
   
   free( line_copy );
   
   return 0;
}

/******* BORROWED FROM libsyndicate.c ********
 * Original author: Jude Nelson
 *********************************************/

// read a configuration line, with the following syntax:
// KEY = "VALUE_1" "VALUE_2" ... "VALUE_N".  Arbitrarily many spaces are allowed.  
// Quotes within quotes are allowed, as long as they are escaped
// return number of values on success (0 for comments and empty lines), negative on syntax/parse error.
// Pass NULL for key and values if you want them allocated for you (otherwise, they must be big enough)
// key and value will remain NULL if the line was a comment.
static int wish_read_conf_line( char* line, char** key, char*** values ) {
   char* line_cpy = strdup( line );
   
   // split on '='
   char* key_half = strtok( line_cpy, "=" );
   if( key_half == NULL ) {
      // ensure that this is a comment or an empty line
      char* key_str = strtok( line_cpy, " \n" );
      if( key_str == NULL ) {
         free( line_cpy );    // empty line
         return 0;
      }
      else if( key_str[0] == COMMENT_KEY ) {
         free( line_cpy );    // comment
         return 0;
      }
      else {
         free( line_cpy );    // bad line
         return -1;
      }
   }
   
   char* value_half = line_cpy + strlen(key_half) + 1;
   
   char* key_str = strtok( key_half, " \t" );
   
   // if this is a comment, then skip it
   if( key_str[0] == COMMENT_KEY ) {
      free( line_cpy );
      return 0;     
   }
   
   // if this is a newline, then skip it
   if( key_str[0] == '\n' || key_str[0] == '\r' ) {
      free( line_cpy );
      return 0;      // just a blank line
   }
   
   // read each value
   vector<char*> val_list;
   char* val_str = strtok( value_half, " \n" );
   
   while( 1 ) {
      if( val_str == NULL )
         // no more values
         break;
      
      // got a value string
      if( val_str[0] != '"' ) {
         // needs to be in quotes!
         free( line_cpy );
         return -3;
      }
      
      // advance beyond the first '"'
      val_str++;
      
      // scan until we find an unescaped "
      int escaped = 0;
      int end = -1;
      for( int i = 0; i < (signed)strlen(val_str); i++ ) {
         if( val_str[i] == '\\' ) {
            escaped = 1;
            continue;
         }
         
         if( escaped ) {
            escaped = 0;
            continue;
         }
         
         if( val_str[i] == '"' ) {
            end = i;
            break;
         }
      }
      
      if( end == -1 ) {
         // the string didn't end in a "
         free( line_cpy );
         return -4;
      }
      
      val_str[end] = 0;
      val_list.push_back( val_str );
      
      // next value
      val_str = strtok( NULL, " \n" );
   }
   
   // get the key
   if( *key == NULL ) {
      *key = (char*)calloc( strlen(key_str) + 1, 1 );
   }
   
   strcpy( *key, key_str );
   
   // get the values
   if( *values == NULL ) {
      *values = (char**)calloc( sizeof(char*) * (val_list.size() + 1), 1 );
   }
   
   for( int i = 0; i < (signed)val_list.size(); i++ ) {
      (*values)[i] = strdup( val_list.at(i) );
   }
   
   free( line_cpy );
   
   return val_list.size();
}


// calculate the concatenation of root and path, and put it in dest if given.
char* fullpath( char const* root, char const* path, char* dest ) {
   char delim = 0;
   int path_off = 0;
   
   int len = strlen(path) + strlen(root) + 2;
   
   if( strlen(root) > 0 ) {
      size_t root_delim_off = strlen(root) - 1;
      if( root[root_delim_off] != '/' && path[0] != '/' ) {
         len++;
         delim = '/';
      }
      else if( root[root_delim_off] == '/' && path[0] == '/' ) {
         path_off = 1;
      }
   }

   if( dest == NULL )
      dest = (char*)calloc( len, 1 );
   
   memset(dest, 0, len);
   
   strcpy( dest, root );
   if( delim != 0 ) {
      dest[strlen(dest)] = '/';
   }
   strcat( dest, path + path_off );
   
   return dest;
}


// recursively make directories
int mkdirs( char const* dirp ) {
   char* currdir = (char*)calloc( strlen(dirp) + 1, 1 );
   unsigned int i = 0;
   while( i <= strlen(dirp) ) {
      if( dirp[i] == '/' || i == strlen(dirp) ) {
         strncpy( currdir, dirp, i == 0 ? 1 : i );
         struct stat statbuf;
         int rc = stat( currdir, &statbuf );
         if( rc == 0 && !S_ISDIR( statbuf.st_mode ) ) {
            free( currdir );
            return -EEXIST;
         }
         if( rc != 0 ) {
            rc = mkdir( currdir, 0755 );
            if( rc != 0 ) {
               free(currdir);
               return -errno;
            }
         }
      }
      i++;
   }
   free(currdir);
   return 0;
}

/*****************************************************/


// parse configuration file
// return 0 on success, -errno on failure
int wish_read_conf( char const* path, struct wish_conf* conf ) {
   FILE* f = fopen( path, "r" );
   if( !f ) {
      return -errno;
   }
   
   memset( conf, 0, sizeof(struct wish_conf) );
   
   char buf[4096];    // should be big enough
   
   char* eof = NULL;
   int line_cnt = 0;
   
   // list of peers
   vector<struct wish_hostent*> peers;
   
   while( true ) {
      memset( buf, 0, sizeof(char) * 4096 );
      eof = fgets( buf, 4096, f );
      if( eof == NULL )
         break;
         
      line_cnt++;
      
      char* key = NULL;
      char** values = NULL;
      int num_values = wish_read_conf_line( buf, &key, &values );
      if( num_values <= 0 ) {
         //dbprintf("read_conf: ignoring malformed line %d\n", line_cnt );
         continue;
      }
      if( key == NULL || values == NULL ) {
         continue;      // comment or empty line
      }
      
      /*************** Add your configuration keys here **********************/
      
      if( strcmp( key, PORTNUM_KEY ) == 0 ) {
         conf->portnum = strtol( values[0], NULL, 10 );
      }
      else if( strcmp( key, CONNECT_TIMEOUT_KEY ) == 0 ) {
         conf->connect_timeout = strtol( values[0], NULL, 10 );
      }
      else if( strcmp( key, USER_ID_KEY ) == 0 ) {
         conf->uid = (unsigned)strtol( values[0], NULL, 10 );
      }
      else if( strcmp( key, DAEMON_BACKLOG_KEY ) == 0 ) {
         conf->daemon_backlog = strtol( values[0], NULL, 10 );
      }
      else if( strcmp( key, FILES_ROOT_KEY ) == 0 ) {
         conf->files_root = strdup( values[0] );
      }
      else if( strcmp( key, STATUS_MEMORY_KEY ) == 0 ) {
         conf->status_memory = strtol( values[0], NULL, 10 );
      }
      else if( strcmp( key, HEARTBEAT_INTERVAL_KEY ) == 0 ) {
         conf->heartbeat_interval = strtoll( values[0], NULL, 10 );
      }
      else if( strcmp( key, PEER_KEY ) == 0 ) {
         struct wish_hostent* host = (struct wish_hostent*)calloc( sizeof(struct wish_hostent), 1 );
         wish_parse_hostent( values[0], host );
         peers.push_back( host );
      }
      else if( strcmp( key, SHELL_KEY ) == 0 ) {
         conf->shell = strdup( values[0] );
      }
      else if( strcmp( key, SHELL_ARGS_KEY ) == 0 ) {
         conf->shell_argv = (char**)calloc( sizeof(char*) * (num_values + 1), 1 );
         for( int i = 0; i < num_values; i++ ) {
            conf->shell_argv[i] = strdup( values[i] );
         }
         conf->shell_argc = num_values;
      }
      else if( strcmp( key, TMP_DIR_KEY ) == 0 ) {
         conf->tmp_dir = strdup( values[0] );
      }
      else if( strcmp( key, SSL_KEY_KEY ) == 0 ) {
         conf->server_key = strdup( values[0] );
      }
      else if( strcmp( key, SSL_CERT_KEY ) == 0 ) {
         conf->server_cert = strdup( values[0] );
      }
      else if( strcmp( key, HTTP_PORTNUM_KEY ) == 0 ) {
         conf->http_portnum = strtol( values[0], NULL, 10 );
      }
      else if( strcmp( key, JOB_TIMEOUT_KEY ) == 0 ) {
         conf->job_timeout = strtol( values[0], NULL, 10 );
      }
      else if( strcmp( key, USE_HTTPS_KEY ) == 0 ) {
         conf->use_https = (strtol(values[0], NULL, 10) != 0);
      }
      else if( strcmp( key, DEBUG_KEY ) == 0 ) {
         _DEBUG = strtol(values[0], NULL, 10);
      }
      
      /***********************************************************************/
      else {
         errorf("wish_read_conf: unknown configuration key %s\n", key );
      }
      
      free( key );
      for( int i = 0; i < num_values; i++ ) {
         free( values[i] );
      }
      free( values );
   }
   
   
   // store peers
   conf->initial_peers = (struct wish_hostent**)calloc( sizeof(wish_hostent*) * (peers.size() + 1), 1 );
   for( unsigned int i = 0; i < peers.size(); i++ )
      conf->initial_peers[i] = peers[i];
   
   if( conf->shell_argv == NULL ) {
      conf->shell_argv = (char**)calloc( sizeof(char*), 1 );
      conf->shell_argv[0] = NULL;
      conf->shell_argc = 0;
   }
   
   fclose(f);
   
   return 0;
}


// read-lock a wish_state
int wish_state_rlock( struct wish_state* state ) {
   return pthread_rwlock_rdlock( &state->lock );
}

// write-lock a wish_state
int wish_state_wlock( struct wish_state* state ) {
   return pthread_rwlock_wrlock( &state->lock );
}

// unlock a wish_state
int wish_state_unlock( struct wish_state* state ) {
   return pthread_rwlock_unlock( &state->lock );
}


// initialize state directories
int wish_init_dirs( struct wish_state* state ) {
   // make sure tmp_dir is defined and exists
   if( !state->conf.tmp_dir ) {
      // try $HOME, and fall back to /tmp/wish-PID if there isn't a $HOME value
      char* home_dir = strdup( getenv("HOME") );
      if( !home_dir ) {
         home_dir = (char*)calloc( strlen("/tmp/wish-XXXXXX"), 1 );
         sprintf(home_dir, "/tmp/wish-%d", getpid() );
      }
      state->conf.tmp_dir = fullpath( home_dir, ".wish/tmp/", NULL );
      errorf("wish_init_dirs: no temporary directory specified; using %s\n", state->conf.tmp_dir);
      free( home_dir );
   }
   
   struct stat sb;
   int rc = stat( state->conf.tmp_dir, &sb );
   if( rc == 0 && !S_ISDIR( sb.st_mode ) ) {
      errorf("wish_init_dirs: %s is not a directory!\n", state->conf.tmp_dir );
      return -ENOTDIR;
   }
   if( rc != 0 ) {
      // need to create?
      rc = mkdirs( state->conf.tmp_dir );
      if( rc != 0 ) {
         errorf("wish_init_dirs: could not create %s!  errno = %d\n", state->conf.tmp_dir, -errno );
         return -errno;
      }
   }
   
   // make sure files_root exists
   if( !state->conf.files_root ) {
      errorf("wish_init_dirs: no file root defined!  Please specify a value for %s in your configuration!\n", FILES_ROOT_KEY );
      return -ENOENT;
   }
   
   rc = stat( state->conf.files_root, &sb );
   if( rc == 0 && !S_ISDIR( sb.st_mode ) ) {
      errorf("wish_init_dirs: %s is not a directory!\n", state->conf.files_root );
      return -ENOTDIR;
   }
   if( rc != 0 ) {
      // need to create?
      rc = mkdirs( state->conf.tmp_dir );
      if( rc != 0 ) {
         errorf("wish_init_dirs: could not create %s!  errno = %d\n", state->conf.files_root, -errno );
         return -errno;
      }
   }
   
   return 0;
}

// library startup
// return 0 on success, -errno on failure
int wish_init( struct wish_state* state ) {
   
   int rc = wish_init_dirs( state );
   if( rc != 0 )
      return rc;
   
   // populate the runtime
   // get the host's addr info
   struct addrinfo hints;
   memset( &hints, 0, sizeof(hints) );
   hints.ai_family = AF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_flags = AI_NUMERICSERV | AI_CANONNAME;
   hints.ai_protocol = 0;
   hints.ai_canonname = NULL;
   hints.ai_addr = NULL;
   hints.ai_next = NULL;
   
   char portnum_buf[10];
   sprintf(portnum_buf, "%d", state->conf.portnum);
   
   struct addrinfo *result = NULL;
   char hostname[HOST_NAME_MAX+1];
   gethostname( hostname, HOST_NAME_MAX );
   
   rc = getaddrinfo( hostname, portnum_buf, &hints, &result );
   if( rc != 0 ) {
      // could not get addr info
      errorf("wish_init: getaddrinfo: %s\n", gai_strerror( rc ) );
      return -abs(rc);
   }
   
   /*
   struct addrinfo *rp = NULL;
   char buf[INET_ADDRSTRLEN];
   printf("AF_INET = %d, AF_INET6 = %d\n", AF_INET, AF_INET6);
   for( rp = result; rp != NULL; rp = rp->ai_next ) {
      inet_ntop( AF_INET, &(((struct sockaddr_in*)(rp->ai_addr))->sin_addr), buf, INET_ADDRSTRLEN );
      
      int port = 0;
      if( rp->ai_addr->sa_family == AF_INET )
         port = ntohs( ((struct sockaddr_in*)(rp->ai_addr))->sin_port );
      else if( rp->ai_addr->sa_family == AF_INET6 )
         port = ntohs( ((struct sockaddr_in6*)(rp->ai_addr))->sin6_port );
      
      printf("address: %s, family = %d, port = %d, canonname = %s\n", buf, rp->ai_addr->sa_family, port, rp->ai_canonname );
   }
   */
   
   // now reverse-lookup ourselves
   char looked_up[HOST_NAME_MAX+1];
   rc = getnameinfo( result->ai_addr, result->ai_addrlen, looked_up, HOST_NAME_MAX, NULL, 0, NI_NAMEREQD );
   if( rc != 0 ) {
      errorf("wish_init: getnameinfo: %s\n", gai_strerror( rc ) );
      return -abs(rc);
   }
   
   // put the first address we get into the state
   state->addr = result;
   
   // make sure state->addr->ai_addr has adequate space
   struct sockaddr_storage* state_addr = (struct sockaddr_storage*)calloc( sizeof(struct sockaddr_storage), 1 );
   memcpy( state_addr, result->ai_addr, result->ai_addrlen );
   state->addr->ai_addr = (struct sockaddr*)state_addr;
   
   state->hostname = strdup( looked_up );
   state->nid = wish_host_nid( looked_up );
   state->fs_invisible = new vector<char*>();
   state->client_cons = new vector<struct wish_connection*>();
   
   // initialize the wish state lock
   pthread_rwlock_init( &state->lock, NULL );
   
   return 0;
}


// library shutdown
// return 0 on success, -errno on failure
int wish_shutdown( struct wish_state* state ) {
   
   if( state->daemon_sock ) 
      close( state->daemon_sock );
   
   if( state->addr )
      freeaddrinfo( state->addr );
   
   if( state->hostname )
      free( state->hostname );
   
   if( state->conf.files_root )
      free( state->conf.files_root );
   
   for( vector<char*>::size_type i = 0; i < state->fs_invisible->size(); i++ ) {
      if( state->fs_invisible->at(i) )
         free( state->fs_invisible->at(i) );
   }
   delete state->fs_invisible;
   
   for( vector<struct wish_connection*>::iterator itr = state->client_cons->begin(); itr != state->client_cons->end(); itr++ ) {
      wish_disconnect( state, (*itr) );
      free( *itr );
   }
   delete state->client_cons;
   
   wish_state_unlock( state );
   
   // free memory
   pthread_rwlock_destroy( &state->lock );
   return 0;
}


// initialize the daemon server
int wish_init_daemon( struct wish_state* state ) {
   
   // make a server socket
   struct addrinfo hints;
   memset( &hints, 0, sizeof(hints) );
   hints.ai_family = AF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
   hints.ai_protocol = 0;
   hints.ai_canonname = NULL;
   hints.ai_addr = NULL;
   hints.ai_next = NULL;
   
   char portnum_buf[10];
   sprintf(portnum_buf, "%d", state->conf.portnum);
   
   struct addrinfo *result = NULL;
   
   int rc = getaddrinfo( NULL, portnum_buf, &hints, &result );
   if( rc != 0 ) {
      // could not get addr info
      errorf("wish_init: getaddrinfo: %s\n", gai_strerror( rc ) );
      freeaddrinfo( result );
      return -abs(rc);
   }
   
   /*
   struct addrinfo *rp = NULL;
   char buf[INET_ADDRSTRLEN];
   printf("AF_INET = %d, AF_INET6 = %d\n", AF_INET, AF_INET6);
   for( rp = result; rp != NULL; rp = rp->ai_next ) {
      inet_ntop( AF_INET, &(((struct sockaddr_in*)(rp->ai_addr))->sin_addr), buf, INET_ADDRSTRLEN );
      
      int port = 0;
      if( rp->ai_addr->sa_family == AF_INET )
         port = ntohs( ((struct sockaddr_in*)(rp->ai_addr))->sin_port );
      else if( rp->ai_addr->sa_family == AF_INET6 )
         port = ntohs( ((struct sockaddr_in6*)(rp->ai_addr))->sin6_port );
      
      printf("address: %s, family = %d, port = %d\n", buf, rp->ai_addr->sa_family, port );
   }
   */
   
   
   // make the socket
   int server_sock = socket( result->ai_family, result->ai_socktype, result->ai_protocol );
   if( server_sock == -1 ) {
      return -errno;
   }
   
   // bind on the socket
   rc = bind( server_sock, result->ai_addr, result->ai_addrlen );
   if( rc != 0 ) {
      close( server_sock );
      return -errno;
   }
   
   // listen on the socket
   rc = listen( server_sock, state->conf.daemon_backlog );
   if( rc != 0 ) {
      close( server_sock );
      return -errno;
   }
   
   // tweak the socket to reuse the address (i.e. if the daemon stops abnormally,
   // the address will still be in use for a time; this fixes that).
   int on = 1;
   rc = setsockopt( server_sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on) );
   if( rc != 0 ) {
      close( server_sock );
      return -errno;
   }
   
   // we're good!
   wish_state_wlock( state );
   state->daemon_sock = server_sock;
   dbprintf("wish_init_daemon: listening for up to %d connections on port %d\n", state->conf.daemon_backlog, state->conf.portnum );
   wish_state_unlock( state );
   
   freeaddrinfo( result );
   
   
   return server_sock;
}


// initialize a packet header
// return 0 on success; negative on error
int wish_init_header( struct wish_state* state, struct wish_packet_header* hdr, uint32_t type ) {
   
   memset( hdr, 0, sizeof(struct wish_packet_header) );
   
   hdr->type = type;
   
   memset( &hdr->origin, 0, sizeof(hdr->origin) );
   
   if( state ) {
      wish_state_rlock( state );
   
      hdr->uid = state->conf.uid;
      memcpy( &hdr->origin, state->addr->ai_addr, state->addr->ai_addrlen );
      
      wish_state_unlock( state );
   }
   else {
      hdr->uid = 0;
   }
   
   hdr->payload_len = 0;
   
   return 0;
}


// initialize a packet, duplicating the payload
int wish_init_packet( struct wish_packet* wp, struct wish_packet_header* hdr, uint8_t* payload, uint32_t len ) {
   hdr->payload_len = len;
   
   if( &wp->hdr != hdr )
      memcpy( &wp->hdr, hdr, sizeof(struct wish_packet_header) );
   
   wp->payload = (uint8_t*)calloc( len, 1 );
   memcpy( wp->payload, payload, len );
   return 0;
}

// initialize a packet, NOT duplicating the payload
int wish_init_packet_nocopy( struct wish_packet* wp, struct wish_packet_header* hdr, uint8_t* payload, uint32_t len ) {
   hdr->payload_len = len;
   
   if( &wp->hdr != hdr )
      memcpy( &wp->hdr, hdr, sizeof(struct wish_packet_header) );
   
   wp->payload = payload;
   return 0;
}


// connect to another daemon, returning 0 on success, or a negative errno
int wish_connect( struct wish_state* state, struct wish_connection* con, char const* hostname, int portnum ) {
   
   // get host info
   struct addrinfo hints;
   memset( &hints, 0, sizeof(hints) );
   hints.ai_family = AF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_flags = AI_NUMERICSERV;
   
   char portnum_str[10];
   sprintf(portnum_str, "%d", portnum );
   
   struct addrinfo *result = NULL;
   
   int rc = getaddrinfo( hostname, portnum_str, &hints, &result );
   if( rc != 0 ) {
      // could not get addr info
      errorf("wish_connect: getaddrinfo: %s\n", gai_strerror( rc ) );
      return -ENETDOWN;
   }
   
   // attempt to connect on each of the possible addresses
   int socket_fd = 0;
   struct addrinfo* rp = NULL;
   for( rp = result; rp != NULL; rp = rp->ai_next ) {
      socket_fd = socket( rp->ai_family, rp->ai_socktype, rp->ai_protocol );
      if( socket_fd <= 0 ) {
         errorf("somehow, socket returned %d\n", socket_fd);
         exit(1);
         continue;
      }
      
      int rc = connect( socket_fd, rp->ai_addr, rp->ai_addrlen );
      if( rc < 0 ) {
         // failed to connect
         close(socket_fd);
         socket_fd = -1;
         continue;
      }
      
      // got a connection if we get this far!
      break;
   }
   
   if( socket_fd < 0 || rp == NULL ) {
      freeaddrinfo( result );
      
      // could not connect
      return -EHOSTDOWN;
   }
   
   // set a socket timeout
   int connect_timeout = 5000;
   if( state ) {
      wish_state_rlock( state );
      
      connect_timeout = state->conf.connect_timeout;
      
      wish_state_unlock( state );
   }
   
   struct timeval tv;
   tv.tv_sec = connect_timeout / 1000;                          // seconds
   tv.tv_usec = (connect_timeout % 1000) * 1000;                // microseconds
   
   rc = setsockopt( socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv) );
   if( rc != 0 ) {
      rc = -errno;
   }
   
   if( rc == 0 ) {
      struct addrinfo* con_addr = (struct addrinfo*)calloc( sizeof(struct addrinfo), 1 );
      memcpy( con_addr, rp, sizeof(struct addrinfo) );
      
      con->soc = socket_fd;
      con->addr = con_addr;
      
      // clone this so we can free correctly
      if( rp->ai_addr ) {
         struct sockaddr_storage* addr = (struct sockaddr_storage*)calloc( rp->ai_addrlen, 1 );
         memcpy( addr, rp->ai_addr, rp->ai_addrlen );
         
         rp->ai_addr = NULL;
         con_addr->ai_addr = (struct sockaddr*)addr;
      }
      
   }
   
   freeaddrinfo( result );
   con->have_header = false;
   con->num_read = 0;
   memset( &con->tmp_hdr, 0, sizeof(con->tmp_hdr) );
   con->last_packet_recved = NULL;
   
   /*
   fprintf(stderr, "%c[%d;%d;%dm", 0x1B, 1, 33, 40);
   errorf("wish_connect: opened %d\n", con->soc);
   fprintf(stderr, "%c[%d;%d;%dm", 0x1B, 0, 37, 40);
   */
   return rc;
}


// duplicate a connection, putting it in next
int wish_connection_clone( struct wish_state* state, struct wish_connection* old, struct wish_connection* next ) {
   next->soc = old->soc;
   next->addr = (struct addrinfo*)calloc( sizeof(struct addrinfo), 1 );
   next->have_header = false;
   next->num_read = 0;
   memset( &next->tmp_hdr, 0, sizeof(next->tmp_hdr) );
   memcpy( next->addr, old->addr, sizeof(struct addrinfo) );
   
   next->last_packet_recved = NULL;
   
   return 0;
}


// free a connection
int wish_connection_free( struct wish_state* state, struct wish_connection* con ) {
   if( con->addr ) {
      free( con->addr->ai_addr );
      free( con->addr );
      con->addr = NULL;
   }
   
   if( con->last_packet_recved ) {
      wish_free_packet( con->last_packet_recved );
      free( con->last_packet_recved );
      con->last_packet_recved = NULL;
   }
   con->have_header = false;
   con->num_read = 0;
   memset( &con->tmp_hdr, 0, sizeof(con->tmp_hdr) );
   con->soc = -1;
   return 0;
}


// disconnect
int wish_disconnect( struct wish_state* state, struct wish_connection* con ) {
   int rc = 0;
   if( con ) {
      con->have_header = false;
      con->num_read = 0;
      memset( &con->tmp_hdr, 0, sizeof(con->tmp_hdr) );
      if( con->soc >= 0 ) {
         /*
         fprintf(stderr, "%c[%d;%d;%dm", 0x1B, 1, 34,40);
         errorf("wish_disconnect: close %d\n", con->soc );
         fprintf(stderr, "%c[%d;%d;%dm", 0x1B, 0, 37, 40);
         */
         rc = close( con->soc );
         con->soc = -1;
      }
      if( con->addr ) {
         free( con->addr->ai_addr );
         free( con->addr );
         con->addr = NULL;
      }
      if( con->last_packet_recved ) {
         wish_free_packet( con->last_packet_recved );
         free( con->last_packet_recved );
         con->last_packet_recved = NULL;
      }
   }
   return rc;
}


// set receive timeout on a wish connection
int wish_recv_timeout( struct wish_state* state, struct wish_connection* con, uint64_t timeout_ms ) {
   if( con->soc > 0 ) {
      struct timeval tv;
      tv.tv_sec = timeout_ms / 1000;                          // seconds
      tv.tv_usec = (timeout_ms % 1000) * 1000;                // microseconds
      
      int rc = setsockopt( con->soc, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv) );
      if( rc != 0 ) {
         rc = -errno;
      }
      return rc;
   }
   else {
      return -EINVAL;
   }
}


// convert a header to network-byte order
static int wish_packet_header_ntoh( struct wish_packet_header* hdr ) {
   hdr->type                    = ntohs( hdr->type );
   hdr->uid                     = ntohl( hdr->uid );
   hdr->origin.ss_family        = ntohs( hdr->origin.ss_family );
   hdr->payload_len             = ntohl( hdr->payload_len );
   return 0;
}

// convert a header to host-byte order
static int wish_packet_header_hton( struct wish_packet_header* hdr ) {
   hdr->type                    = htons( hdr->type );
   hdr->uid                     = htonl( hdr->uid );
   hdr->origin.ss_family        = htons( hdr->origin.ss_family );
   hdr->payload_len             = htonl( hdr->payload_len );
   return 0;
}


// read a (default) packet from a socket.
// return 0 on success, -errno on failure
int wish_read_packet_impl( struct wish_state* state, struct wish_connection* con, struct wish_packet* wp, int flags ) {
   /*
   fprintf(stderr, "%c[%d;%d;%dm", 0x1B, 1, 32, 40);
   errorf("wish_read_packet on %d\n", con->soc );
   fprintf(stderr, "%c[%d;%d;%dm", 0x1B, 0, 37, 40);
   */
   
   if( !con->have_header ) {
      
      // get the header
      while( con->num_read < (signed)sizeof(con->tmp_hdr) ) {
         errno = 0;
         ssize_t rd_cnt = recv( con->soc, ((uint8_t*)&con->tmp_hdr) + con->num_read, sizeof(con->tmp_hdr) - con->num_read, flags );
         if( rd_cnt > 0 ) {
            con->num_read += rd_cnt;
         }
         else {
            if( rd_cnt == 0 ) {
               // connection is closed
               return -EHOSTDOWN;
            }
            else if( (flags & MSG_DONTWAIT) && (errno == EAGAIN || errno == EWOULDBLOCK) ) {
               // non-blocking I/O requested, and it failed
               return -EAGAIN;
            }
            else {
               int errsv = -errno;
               errorf("wish_read_packet_impl: errno = %d when reading header from %d\n", errsv, con->soc );
               return errsv;
            }
         }
      }
      
      // convert to host byte order
      wish_packet_header_ntoh( &con->tmp_hdr );
      
      // sanity check--make sure that the length is sensible
      if( con->tmp_hdr.payload_len > WISH_MAX_PACKET_SIZE ) {
         // nonsensical size
         errorf("wish_read_packet_impl: nonsensical payload length %u\n", con->tmp_hdr.payload_len );
         if( con->last_packet_recved ) {
            wish_free_packet( con->last_packet_recved );
            free( con->last_packet_recved );
            con->last_packet_recved = NULL;
         }
         return -ENOMSG;
      }
      
      // store this header
      if( con->last_packet_recved ) {
         wish_free_packet( con->last_packet_recved );
         memset( con->last_packet_recved, 0, sizeof(struct wish_packet) );
      }
      else {
         con->last_packet_recved = (struct wish_packet*)calloc( sizeof(struct wish_packet), 1 );
      }
      
      memcpy( &con->last_packet_recved->hdr, &con->tmp_hdr, sizeof(struct wish_packet_header) );
      
      // allocate the packet payload
      con->last_packet_recved->payload = (uint8_t*)calloc( con->last_packet_recved->hdr.payload_len, 1 );
      con->have_header = true;
      con->num_read = 0;
      memset( &con->tmp_hdr, 0, sizeof(struct wish_packet_header) );
   }
   
   if( con->have_header ) {
      // get the body
      
      while( (unsigned)con->num_read < con->last_packet_recved->hdr.payload_len ) {
         errno = 0;
         ssize_t rd_cnt = recv( con->soc, con->last_packet_recved->payload + con->num_read, con->last_packet_recved->hdr.payload_len - con->num_read, flags );
         if( rd_cnt > 0 ) {
            con->num_read += rd_cnt;
         }
         else {
            if( rd_cnt == 0 ) {
               // connection is closed
               return -EHOSTDOWN; 
            }
            else if( (flags & MSG_DONTWAIT) && (errno == EAGAIN || errno == EWOULDBLOCK) ) {
               // non-blocking I/O requested, and it failed
               return -EAGAIN;
            }
            else {
               int errsv = -errno;
               errorf("wish_read_packet_impl: errno = %d when reading payload from %d\n", errsv, con->soc );
               return errsv;
            }
         }
      }
      
      // got the packet!
      memcpy( wp, con->last_packet_recved, sizeof(struct wish_packet));
      
      con->have_header = false;
      con->num_read = 0;
      free( con->last_packet_recved );
      con->last_packet_recved = NULL;
   }
   
   return 0;
}


// get a packet; block until we have it
int wish_read_packet( struct wish_state* state, struct wish_connection* con, struct wish_packet* wp ) {
   return wish_read_packet_impl( state, con, wp, 0 );
}

// get a packet; don't block (return -EAGAIN if no data is available)
int wish_read_packet_noblock( struct wish_state* state, struct wish_connection* con, struct wish_packet* wp ) {
   return wish_read_packet_impl( state, con, wp, MSG_DONTWAIT );
}


// clear out a connection
int wish_clear_connection( struct wish_state* state, struct wish_connection* con ) {
   while( 1 ) {
      char buf[1024];
      ssize_t numr = recv( con->soc, buf, 1024, MSG_DONTWAIT );
      if( numr <= 0 )
         return 0;
   }
   return 0;
}

// wait for a client or another daemon to connect to the daemon
int wish_accept( struct wish_state* state, struct wish_connection* con ) {
   struct sockaddr_storage* addr = (struct sockaddr_storage*)calloc( sizeof(struct sockaddr_storage), 1 );
   socklen_t addrlen = sizeof(struct sockaddr_storage);
   
   wish_state_rlock( state );
   int server_fd = state->daemon_sock;
   wish_state_unlock( state );
   
   errno = 0;
   int client_soc = accept( server_fd, (struct sockaddr*)addr, &addrlen );
   if( client_soc >= 0 ) {
      // success!
      memset( con, 0, sizeof(struct wish_connection) );
      
      con->addr = (struct addrinfo*)calloc( sizeof(struct addrinfo), 1 );
      con->addr->ai_addr = (struct sockaddr*)addr;
      con->addr->ai_addrlen = addrlen;
      con->addr->ai_socktype = SOCK_STREAM;     // TODO: extract from accepted connection
      con->addr->ai_protocol = IPPROTO_TCP;     // TODO: extract from accepted connection
      con->addr->ai_flags = 0;                  // TODO: extract from accepted connection
      con->addr->ai_family = addr->ss_family;
      con->addr->ai_canonname = NULL;
      con->addr->ai_next = NULL;
      con->soc = client_soc;
      
      fprintf(stderr, "%c[%d;%d;%dm", 0x1B, 1, 33, 40);
      errorf("wish_accept: opened %d\n", client_soc);
      fprintf(stderr, "%c[%d;%d;%dm", 0x1B, 0, 37, 40);
   
      return 0;
   }
   else {
      // failure!
      free( addr );
      return -errno;
   }
}


// write a (default) packet to a socket
// return 0 on success, -errno on failure
int wish_write_packet( struct wish_state* state, struct wish_connection* con, struct wish_packet* wp ) {
   // write the header
   /*
   fprintf(stderr, "%c[%d;%d;%dm", 0x1B, 1, 35, 40);
   errorf("wish_write_packet: %u bytes on %d\n", wp->hdr.payload_len, con->soc );
   fprintf(stderr, "%c[%d;%d;%dm", 0x1B, 0, 37, 40);
   */
   
   struct wish_packet_header tmp_hdr;
   memcpy( &tmp_hdr, &wp->hdr, sizeof(tmp_hdr) );
   
   wish_packet_header_hton( &tmp_hdr );
   
   ssize_t num_written = 0;
   while( num_written < (signed)sizeof(tmp_hdr) ) {
      errno = 0;
      ssize_t numw = send( con->soc, ((uint8_t*)&tmp_hdr) + num_written, sizeof(tmp_hdr) - num_written, 0 );
      
      if( numw > 0 ) {
         num_written += numw;
      }
      else {
         // error
         int errsv = -errno;
         errorf("wish_write_packet: rc = %ld, errno = %d when writing header to %d\n", numw, errsv, con->soc );
         return errsv;
      }
   }
   
   // write the payload
   num_written = 0;
   while( (unsigned)num_written < wp->hdr.payload_len ) {
      errno = 0;
      ssize_t numw = send( con->soc, wp->payload + num_written, wp->hdr.payload_len - num_written, 0 );
      
      if( numw > 0 ) {
         num_written += numw;
      }
      else {
         // error
         int errsv = -errno;
         errorf("wish_write_packet: rc = %ld, errno = %d when writing payload to %d\n", numw, errsv, con->soc );
         return errsv;
      }
   }
   
   // success!
   return 0;
}


// free a packet's memory
int wish_free_packet( struct wish_packet* wp ) {
   if( wp->payload )
      free( wp->payload );
   
   memset( wp, 0, sizeof(struct wish_packet) );
   return 0;
}

// pack a character
void wish_pack_char( uint8_t* buf, off_t* offset, char value ) {
   buf[ *offset ] = (uint8_t)value;
   (*offset)++;
}


// pack a byte
void wish_pack_byte( uint8_t* buf, off_t* offset, int8_t value ) {
   buf[ *offset ] = (uint8_t)value;
   (*offset)++;
}

// pack an unsigned byte
void wish_pack_ubyte( uint8_t* buf, off_t* offset, uint8_t value ) {
   buf[ *offset ] = value;
   (*offset)++;
}

// pack a short
void wish_pack_short( uint8_t* buf, off_t* offset, int16_t value ) {
   int16_t tmp = htons( value );
   memcpy( buf + *offset, &tmp, sizeof(tmp) );
   *offset += sizeof(tmp);
}

// pack an unsigned short
void wish_pack_ushort( uint8_t* buf, off_t* offset, uint16_t value ) {
   uint16_t tmp = htons( value );
   memcpy( buf + *offset, &tmp, sizeof(tmp) );
   *offset += sizeof(tmp);
}

// pack an int
void wish_pack_int( uint8_t* buf, off_t* offset, int32_t value ) {
   int32_t tmp = htonl( value );
   memcpy( buf + *offset, &tmp, sizeof(tmp) );
   *offset += sizeof(tmp);
}

// pack an unsigned int
void wish_pack_uint( uint8_t* buf, off_t* offset, uint32_t value ) {
   uint32_t tmp = htonl( value );
   memcpy( buf + *offset, &tmp, sizeof(tmp) );
   *offset += sizeof(tmp);
}

// pack a long
void wish_pack_long( uint8_t* buf, off_t* offset, int64_t value ) {
   wish_pack_ulong( buf, offset, (uint64_t)value );
}

// pack an unsigned long
void wish_pack_ulong( uint8_t* buf, off_t* offset, uint64_t value ) {
   uint32_t tmp_low = htonl( (uint32_t)(value & (uint64_t)0xFFFFFFFF) );
   uint32_t tmp_high = htonl( (uint32_t)(value >> 32) );
   
   memcpy( buf + *offset, &tmp_low, sizeof(tmp_low) );
   *offset += sizeof(tmp_low);
   
   memcpy( buf + *offset, &tmp_high, sizeof(tmp_high) );
   *offset += sizeof(tmp_high);
}


// pack a string
void wish_pack_string( uint8_t* buf, off_t* offset, char* value ) {
   size_t len = strlen( value ) + 1;
   memcpy( buf + *offset, (uint8_t*)value, len );
   *offset += len;
}

// pack a sockaddr
void wish_pack_sockaddr( uint8_t* buf, off_t* offset, struct sockaddr_storage* value ) {
   value->ss_family = htons( value->ss_family );
   memcpy( buf + *offset, value, sizeof(struct sockaddr_storage) );
   value->ss_family = ntohs( value->ss_family );
   *offset += sizeof(struct sockaddr_storage);
}

// unpack a character
char wish_unpack_char( uint8_t* buf, off_t* offset ) {
   char ret = (char)buf[ *offset ];
   *offset += sizeof(ret);
   return ret;
}

// unpack a byte
int8_t wish_unpack_byte( uint8_t* buf, off_t* offset ) {
   int8_t ret = (int8_t)buf[ *offset ];
   *offset += sizeof(ret);
   return ret;
}

// unpack an unsigned byte
uint8_t wish_unpack_ubyte( uint8_t* buf, off_t* offset ) {
   uint8_t ret = (uint8_t)buf[ *offset ];
   *offset += sizeof(ret);
   return ret;
}

// unpack a short
int16_t wish_unpack_short( uint8_t* buf, off_t* offset ) {
   int16_t ret;
   memcpy( &ret, buf + *offset, sizeof(ret) );
   ret = ntohs( ret );
   *offset += sizeof(ret);
   return ret;
}

// unpack an unsigned short
uint16_t wish_unpack_ushort( uint8_t* buf, off_t* offset ) {
   uint16_t ret;
   memcpy( &ret, buf + *offset, sizeof(ret) );
   ret = ntohs( ret );
   *offset += sizeof(ret);
   return ret;
}

// unpack an int
int32_t wish_unpack_int( uint8_t* buf, off_t* offset ) {
   int32_t ret;
   memcpy( &ret, buf + *offset, sizeof(ret) );
   ret = ntohl( ret );
   *offset += sizeof(ret);
   return ret;
}

// unpack an unsigned int
uint32_t wish_unpack_uint( uint8_t* buf, off_t* offset ) {
   uint32_t ret;
   memcpy( &ret, buf + *offset, sizeof(ret) );
   ret = ntohl( ret );
   *offset += sizeof(ret);
   return ret;
}

// unpack a long
int64_t wish_unpack_long( uint8_t* buf, off_t* offset ) {
   return (int64_t)wish_unpack_ulong( buf, offset );
}

// unpack an unsigned long
uint64_t wish_unpack_ulong( uint8_t* buf, off_t* offset ) {
   uint32_t low, high;
   
   memcpy( &low, buf + *offset, sizeof(low) );
   *offset += sizeof(low);
   
   memcpy( &high, buf + *offset, sizeof(high) );
   *offset += sizeof(high);
   
   low = ntohl( low );
   high = ntohl( high );
   
   uint64_t ret = ((uint64_t)high << 32) | low;
   return ret;
}

// unpack a string
char* wish_unpack_string( uint8_t* buf, off_t* offset ) {
   char* ret = strdup( (char*)(buf + *offset) );
   *offset += strlen(ret) + 1;
   return ret;
}

// unpack a socket address
struct sockaddr_storage* wish_unpack_sockaddr( uint8_t* buf, off_t* offset ) {
   struct sockaddr_storage* addr = (struct sockaddr_storage*)calloc( sizeof(struct sockaddr_storage), 1 );
   memcpy( addr, buf + *offset, sizeof(struct sockaddr_storage) );
   addr->ss_family = ntohs( addr->ss_family );
   *offset += sizeof(struct sockaddr_storage);
   return addr;
}


// load a file into RAM
// return a pointer to the bytes.
// set the size.
char* wish_load_file( char* path, size_t* size ) {
   struct stat statbuf;
   int rc = stat( path, &statbuf );
   if( rc != 0 )
      return NULL;
   
   char* ret = (char*)calloc( statbuf.st_size, 1 );
   if( ret == NULL )
      return NULL;
   
   FILE* f = fopen( path, "r" );
   if( !f ) {
      free( ret );
      return NULL;
   }
   
   *size = fread( ret, 1, statbuf.st_size, f );
   fclose( f );
   return ret;
}


// ****************** borrowed from libsyndicate.c ********************************
// download data to disk
static size_t download_disk(void *stream, size_t size, size_t count, void* user_data) {
   int* fd = (int*)user_data;
   
   ssize_t num_written = size*count;
   
   if( *fd > 0 ) {
      num_written = write( *fd, stream, size*count );
      if( num_written < 0 )
         num_written = 0;
   }
   
   return (size_t)num_written;
}

// download data to disk
static size_t download_ram(void *stream, size_t size, size_t count, void* user_data) {
   struct wish_HTTP_buf* buf = (struct wish_HTTP_buf*)user_data;
   
   ssize_t num_written = size*count;
   
   if( buf->offset + num_written >= buf->size ) {
      num_written = buf->size - buf->offset;
   }
   memcpy( buf->data, (char*)stream + buf->offset, num_written );
   
   buf->offset += num_written;
   
   return (size_t)num_written;
}

// get a file and save it to disk
static int wish_HTTP_download( struct wish_state* state, struct wish_HTTP_info* resp, char const* url, char const* username, char const* password, size_t (*download_func)(void*, size_t, size_t, void*), void* data ) {
   
   CURL* curl_h = curl_easy_init();
   curl_easy_setopt( curl_h, CURLOPT_NOPROGRESS, 1L );   // no progress bar
   curl_easy_setopt( curl_h, CURLOPT_USERAGENT, "wish/1.0");
   curl_easy_setopt( curl_h, CURLOPT_URL, url );
   curl_easy_setopt( curl_h, CURLOPT_FOLLOWLOCATION, 1L );
   
   char* userpass = NULL;
   if( username && password ) {
      // obsolete CURL interface, but necessary for planetlab
      curl_easy_setopt( curl_h, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC );
      userpass = (char*)calloc( strlen(username) + 1 + strlen(password) + 1, 1 );
      sprintf( userpass, "%s:%s", username, password );
      curl_easy_setopt( curl_h, CURLOPT_USERPWD, userpass );
   }
   
   curl_easy_setopt( curl_h, CURLOPT_NOSIGNAL, 1L );
   curl_easy_setopt( curl_h, CURLOPT_WRITEFUNCTION, download_func );
   curl_easy_setopt( curl_h, CURLOPT_WRITEDATA, data );
   
   if( state ) {
      wish_state_rlock( state );
      curl_easy_setopt( curl_h, CURLOPT_CONNECTTIMEOUT, state->conf.connect_timeout );
      wish_state_unlock( state );
   }
   
   int rc = curl_easy_perform( curl_h );
   if( rc == 0 ) {
      // success!
      // populate our response
      memset( resp, 0, sizeof(struct wish_HTTP_info) );
      
      curl_easy_getinfo( curl_h, CURLINFO_RESPONSE_CODE, &resp->status );
      curl_easy_getinfo( curl_h, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &resp->size );
      
      if( resp->status >= 400 ) {
         rc = -resp->status;
      }
      
      char* type = NULL;
      curl_easy_getinfo( curl_h, CURLINFO_CONTENT_TYPE, &type );
      resp->mimetype = strdup( type );
   }
   
   curl_easy_cleanup( curl_h );
   if( userpass )
      free( userpass );
   
   return -abs(rc);
}

// get a file to disk and save it
int wish_HTTP_download_file( struct wish_state* state, struct wish_HTTP_info* resp, char const* url, char const* username, char const* password, int fd ) {
   return wish_HTTP_download( state, resp, url, username, password, download_disk, &fd );
}

// get a file to RAM and save it
int wish_HTTP_download_ram( struct wish_state* state, struct wish_HTTP_info* resp, char const* url, char const* username, char const* password, struct wish_HTTP_buf* buf ) {
   return wish_HTTP_download( state, resp, url, username, password, download_ram, buf );
}


// allocate an HTTP buffer
int wish_make_HTTP_buf( struct wish_HTTP_buf* buf, ssize_t size ) {
   buf->size = size;
   buf->offset = 0;
   buf->data = (char*)calloc( size, 1 );
   if( buf->data == NULL )
      return -1;
   
   return 0;
}

// free an HTTP buffer
int wish_free_HTTP_buf( struct wish_HTTP_buf* buf ) {
   if( buf->data ) {
      free( buf->data );
      buf->data = NULL;
   }
   memset( buf, 0, sizeof(struct wish_HTTP_buf) );
   return 0;
}


// get the basename of a path
char* wish_basename( char const* path, char* dest ) {
   int delim_i = strlen(path) - 1;
   if( path[delim_i] == '/' ) {
      // this path ends with '/', so skip over it if it isn't /
      if( delim_i > 0 ) {
         delim_i--;
      }
   }
   for( ; delim_i >= 0; delim_i-- ) {
      if( path[delim_i] == '/' )
         break;
   }
   delim_i++;
   
   if( dest == NULL ) {
      dest = (char*)calloc( strlen(path) - delim_i + 1, 1 );
   }
   else {
      memset( dest, 0, strlen(path) - delim_i );
   }
   strncpy( dest, path + delim_i, strlen(path) - delim_i );
   return dest;
}

int wish_free_HTTP_info( struct wish_HTTP_info* info ) {
   if( info->mimetype ) {
      free( info->mimetype );
      info->mimetype = NULL;
   }
   return 0;
}


// **********************************************************************************


uint64_t wish_time_millis() {
   struct timeval tp;
   gettimeofday(&tp, NULL);
   return tp.tv_sec * 1000 + (tp.tv_usec / 1000);
}
