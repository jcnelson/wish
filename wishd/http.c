#include "http.h"


// calculate the sha-1 hash of something.
// caller must free the hash buffer.
unsigned char* sha1_hash_data( char* input, size_t len ) {
   unsigned char* obuf = (unsigned char*)calloc( SHA_DIGEST_LENGTH, 1 );
   
   SHA1( (unsigned char*)input, strlen(input), obuf );
   return obuf;
}

// calculate the sha-1 hash of a string
unsigned char* sha1_hash( char* input ) {
   return sha1_hash_data( input, strlen(input) );
}

// make a sha-1 hash printable
char* sha1_printable( unsigned char* sha1 ) {
   char* ret = (char*)calloc( sizeof(char) * (2 * SHA_DIGEST_LENGTH + 1), 1 );
   
   char buf[3];
   for( int i = 0; i < SHA_DIGEST_LENGTH; i++ ) {
      sprintf(buf, "%02x", sha1[i] );
      ret[2*i] = buf[0];
      ret[2*i + 1] = buf[1];
   }
   
   return ret;
}

// respond to a request
static int wish_HTTP_default_send_response( struct MHD_Connection* connection, int status_code, char* data ) {
   char const* internal_server_error = "Internal Server Error\n";
   char const* bad_request = "Bad Request\n";
   char const* authentication_required = "Invalid authorization credentials\n";
   char const* default_page = "RESPONSE";
   
   char* page = NULL;
   struct MHD_Response* response = NULL;
   
   if( data == NULL ) {
      // use a built-in status message
      switch( status_code ) {
         case MHD_HTTP_BAD_REQUEST:
            page = (char*)bad_request;
            break;
         
         case MHD_HTTP_INTERNAL_SERVER_ERROR:
            page = (char*)internal_server_error;
            break;
         
         case MHD_HTTP_UNAUTHORIZED:
            page = (char*)authentication_required;
            break;
            
         default:
            page = (char*)default_page;
            break;
      }
      response = MHD_create_response_from_buffer( strlen(page), (void*)page, MHD_RESPMEM_MUST_COPY );
   }
   else {
      // use the given status message
      response = MHD_create_response_from_buffer( strlen(data), (void*)data, MHD_RESPMEM_MUST_FREE );
   }
   
   if( !response )
      return MHD_NO;
      
   // this is always a text/plain type
   MHD_add_response_header( response, "Content-Type", "text/plain" );
   int ret = MHD_queue_response( connection, status_code, response );
   MHD_destroy_response( response );
   
   return ret;
}


// give back a user-callback-created response
static int wish_HTTP_send_response( struct MHD_Connection* connection, struct HTTP_response* resp ) {
       
   struct MHD_Response* response = NULL;
   if( resp->type == HTTP_RESPONSE_RAM ) {
      response = MHD_create_response_from_buffer( resp->ram_data.len, resp->ram_data.data, MHD_RESPMEM_MUST_COPY );
   }
   else {
      int new_fd = dup( fileno( resp->file_data.f ) );
      fclose( resp->file_data.f );
      response = MHD_create_response_from_fd_at_offset( resp->file_data.size, new_fd, resp->file_data.offset );
   }
   
   // this is always a text/plain type
   MHD_add_response_header( response, "Content-Type", resp->mimetype );
   
   if( resp->headers ) {
      struct HTTP_header** headers = resp->headers;
      for( int i = 0; headers[i] != NULL; i++ ) {
         MHD_add_response_header( response, headers[i]->header, headers[i]->value );
      }
   }
   
   int ret = MHD_queue_response( connection, resp->status, response );
   MHD_destroy_response( response );
   return ret;
}


// free an HTTP response
void wish_free_HTTP_response( struct HTTP_response* resp ) {
   if( resp ) {
      free( resp->mimetype );
      if( resp->headers ) {
         for( int i = 0; resp->headers[i] != NULL; i++ ) {
            wish_free_HTTP_header( resp->headers[i] );
            free( resp->headers[i] );
         }
         free( resp->headers );
      }
      if( resp->type == HTTP_RESPONSE_RAM ) {
         if( resp->ram_data.data ) {
            free( resp->ram_data.data );
         }
      }
   }
}

// find a user entry, given a username
struct HTTP_user_entry* wish_find_user_entry( char* username, struct HTTP_user_entry** users ) {
   for( int i = 0; users[i] != NULL; i++ ) {
      if( strcmp( users[i]->username, username ) == 0 )
         return users[i];
   }
   return NULL;
}


// find a user entry, given a uid
struct HTTP_user_entry* wish_find_user_entry2( uid_t uid, struct HTTP_user_entry** users ) {
   for( int i = 0; users[i] != NULL; i++ ) {
      if( users[i]->uid == uid )
         return users[i];
   }
   return NULL;
}

// validate a user entry, given the password
bool wish_validate_user_password( char* password, struct HTTP_user_entry* uent ) {
   // hash the password
   unsigned char* password_sha1 = sha1_hash( password );
   char* password_hash = sha1_printable( password_sha1 );
   free( password_sha1 );
   
   bool rc = (strcasecmp( password_hash, uent->password_hash ) == 0 );
   
   free( password_hash );
   return rc;
}

// create an HTTP response with RAM
int wish_create_HTTP_response_ram( struct HTTP_response* resp, char const* mimetype, int status, char* data, int len ) {
   resp->type = HTTP_RESPONSE_RAM;
   resp->status = status;
   resp->ram_data.data = (char*)calloc( len, 1 );
   memcpy( resp->ram_data.data, data, len );
   resp->ram_data.len = len;
   
   if( mimetype == NULL )
      resp->mimetype = strdup( "text/plain" );
   else
      resp->mimetype = strdup( mimetype );
   
   return 0;
}

// create an HTTP response on disk
int wish_create_HTTP_response_disk( struct HTTP_response* resp, char const* mimetype, int status, FILE* file, off_t offset, size_t size ) {
   resp->type = HTTP_RESPONSE_FILE;
   resp->status = status;
   resp->file_data.size = size;
   resp->file_data.offset = offset;
   resp->file_data.f = file;
   
   if( mimetype == NULL )
      resp->mimetype = strdup( "text/plain" );
   else
      resp->mimetype = strdup( mimetype );
   
   return 0;
}

// get the user's data out of a syndicate-managed connection data structure
void* wish_cls_get( void* cls ) {
   struct HTTP_connection_data* dat = (struct HTTP_connection_data*)cls;
   return dat->cls;
}

// set the status of a syndicate-managed connection data structure
void wish_cls_set_status( void* cls, int status ) {
   struct HTTP_connection_data* dat = (struct HTTP_connection_data*)cls;
   dat->status = status;
}


// create an http header
void wish_create_HTTP_header( struct HTTP_header* header, char const* h, char const* v ) {
   header->header = strdup( h );
   header->value = strdup( v );
}

// create an http cgi arg
void wish_create_HTTP_CGI_arg( struct HTTP_CGI_arg* arg, char const* h, char const* v ) {
   arg->name = strdup( h );
   
   if( v ) 
      arg->value = strdup( v );
}


// free an HTTP header
void wish_free_HTTP_header( struct HTTP_header* header ) {
   if( header->header ) {
      free( header->header );
   }
   if( header->value ) {
      free( header->value );
   }
   memset( header, 0, sizeof(struct HTTP_header) );
}

// free a download buffer
void wish_free_download_buf( struct HTTP_download_buf* buf ) {
   if( buf->data ) {
      free( buf->data );
      buf->data = NULL;
   }
   buf->len = 0;
}

// accumulate inbound headers
static int wish_accumulate_headers( void* cls, enum MHD_ValueKind kind, char const* key, char const* value ) {
   vector<struct HTTP_header*> *header_list = (vector<struct HTTP_header*> *)cls;
   
   struct HTTP_header* hdr = (struct HTTP_header*)calloc( sizeof(struct HTTP_header), 1 );
   wish_create_HTTP_header( hdr, key, value );
   
   header_list->push_back( hdr );
   return MHD_YES;
}


// HTTP connection handler
static int wish_HTTP_connection_handler( void* cls, struct MHD_Connection* connection, 
                                         char const* url, 
                                         char const* method, 
                                         char const* version, 
                                         char const* upload_data, size_t* upload_size, 
                                         void** con_cls ) {
   
   struct HTTP* args = (struct HTTP*)cls;
   
   struct HTTP_user_entry** users = args->users;
   
   // need to create connection data?
   if( *con_cls == NULL ) {
      
      struct HTTP_connection_data* con_data = (struct HTTP_connection_data*)calloc( sizeof( struct HTTP_connection_data ), 1 );
      if( !con_data )
         return MHD_NO;
         
      struct HTTP_user_entry* uent = NULL;
      if( users ) {
         // authenticate the user, if users are given
         char* password = NULL;
         char* username = MHD_basic_auth_get_username_password( connection, &password );
         
         char* password_ptr = password;
         if( password_ptr == NULL )
            password_ptr = (char*)"";
         
         if( username ) {
            uent = wish_find_user_entry( username, users );
            if( uent == NULL ) {
               // user does not exist
               dbprintf("wish_HTTP_connection_handler: user '%s' not found\n", username);
               free( username );
               free( con_data );
               return wish_HTTP_default_send_response( connection, MHD_HTTP_UNAUTHORIZED, NULL );
            }
            if( !wish_validate_user_password( password_ptr, uent ) ) {
               // invalid password
               dbprintf("wish_HTTP_connection_handler: invalid password for user '%s'\n", username);
               free( username );
               free( con_data );
               return wish_HTTP_default_send_response( connection, MHD_HTTP_UNAUTHORIZED, NULL );
            }
            
            free( username );
         }
         
         if( password )
            free( password );
      }
      else {
         // make sure we don't need authentication
         if( strcmp( method, "GET") == 0 && (args->authentication_mode & HTTP_AUTHENTICATE_READ) ) {
            // authentication is needed
            errorf("%s: no username given, but we require authentication for GET (READ)\n", "wish_HTTP_connection_handler");
            return wish_HTTP_default_send_response( connection, MHD_HTTP_UNAUTHORIZED, NULL );
         }
         else if( strcmp( method, "POST" ) == 0 && (args->authentication_mode & HTTP_AUTHENTICATE_WRITE) ) {
            // authentication is needed
            errorf("%s: no username given, but we require authentication for POST (WRITE)\n", "wish_HTTP_connection_handler");
            return wish_HTTP_default_send_response( connection, MHD_HTTP_UNAUTHORIZED, NULL );
         }
      }
      
      int mode = 0;
      struct MHD_PostProcessor* pp = NULL;
      
      
      if( strcmp( method, "GET" ) == 0 ) {
         mode = HTTP_GET;
      }
      else if( strcmp( method, "POST" ) == 0 ) {
         
         if( args->HTTP_POST_handler ) {
            pp = MHD_create_post_processor( connection, 4096, args->HTTP_POST_handler, con_data );
            if( pp == NULL ) {
               free( con_data );
               return MHD_NO;
            }
         }
         
         mode = HTTP_POST;
      }
      else {
         mode = HTTP_UNKNOWN;
      }
      
      con_data->user = uent;
      con_data->pp = pp;
      con_data->status = MHD_HTTP_OK;
      con_data->mode = mode;
      
      if( args->HTTP_connect )
         con_data->cls = (*(args->HTTP_connect))( args, mode, uent );
      
      *con_cls = con_data;
      
      return MHD_YES;
   }
   
   // GET
   if( strcmp( method, "GET" ) == 0 ) {
      if( args->HTTP_GET_handler ) {
         dbprintf("GET url=%s\n", url);
         
         // get headers
         vector<struct HTTP_header*> headers_vec;
         MHD_get_connection_values( connection, MHD_HEADER_KIND, wish_accumulate_headers, (void*)&headers_vec );
         
         // convert to list
         struct HTTP_header** headers = (struct HTTP_header**)calloc( sizeof(struct HTTP_header*) * (headers_vec.size() + 1), 1 );
         for( unsigned int i = 0; i < headers_vec.size(); i++ ) {
            headers[i] = headers_vec.at(i);
         }
         
         struct HTTP_connection_data* con_data = (struct HTTP_connection_data*)(*con_cls);
         struct HTTP_response* resp = (*args->HTTP_GET_handler)( args, con_data->user, con_data->cls, (char*)url, (char*)version, headers);
         
         if( !resp ) {
            // make a default response
            resp = (struct HTTP_response*)calloc( sizeof(struct HTTP_response), 1 );
            
            char buf[100];
            sprintf(buf, "Internal Server Error");
            
            wish_create_HTTP_response_ram( resp, "text/plain", 500, buf, strlen(buf) );
         }
         
         con_data->resp = resp;
         
         for( unsigned int i = 0; headers[i] != NULL; i++ ) {
            wish_free_HTTP_header( headers[i] );
            free( headers[i] );
         }
         free( headers );
         
         return wish_HTTP_send_response( connection, resp );
      }
   }
   
   // POST
   if( strcmp( method, "POST" ) == 0 ) {
      if( args->HTTP_POST_handler ) {
         dbprintf("POST url=%s, size = %ld\n", url, *upload_size );
      
         struct HTTP_connection_data* con_data = (struct HTTP_connection_data*)(*con_cls);
         
         if( *upload_size != 0 ) {
            MHD_post_process( con_data->pp, upload_data, *upload_size );
            *upload_size = 0;
            return MHD_YES;
         }
         else {
            return wish_HTTP_default_send_response( connection, con_data->status, NULL );
         }
      }
   }
   
   // HEAD
   if( strcmp( method, "HEAD" ) == 0 ) {
      if( args->HTTP_HEAD_handler != NULL ) {
         dbprintf("HEAD url=%s\n", url );
         
         // get headers
         vector<struct HTTP_header*> headers_vec;
         MHD_get_connection_values( connection, MHD_HEADER_KIND, wish_accumulate_headers, (void*)&headers_vec );
         
         // convert to list
         struct HTTP_header** headers = (struct HTTP_header**)calloc( sizeof(struct HTTP_header*) * (headers_vec.size() + 1), 1 );
         for( unsigned int i = 0; i < headers_vec.size(); i++ ) {
            headers[i] = headers_vec[i];
         }
         
         struct HTTP_connection_data* con_data = (struct HTTP_connection_data*)(*con_cls);
         
         struct HTTP_response* resp = (*args->HTTP_HEAD_handler)( args, con_data->user, con_data->cls, (char*)url, (char*)version, headers );
         
         if( !resp ) {
            // make a default response
            resp = (struct HTTP_response*)calloc( sizeof(struct HTTP_response), 1 );
            
            char buf[100];
            sprintf(buf, "Internal Server Error");
            
            wish_create_HTTP_response_ram( resp, "text/plain", 500, buf, strlen(buf) );
         }
         for( unsigned int i = 0; headers[i] != NULL; i++ ) {
            wish_free_HTTP_header( headers[i] );
            free( headers[i] );
         }
         free( headers );
         
         return wish_HTTP_send_response( connection, resp );
      }
   }
   
   return wish_HTTP_default_send_response( connection, MHD_HTTP_BAD_REQUEST, NULL );
}

// default cleanup handler
// calls user-supplied cleanup handler as well
void wish_HTTP_cleanup( void *cls, struct MHD_Connection *connection, void **con_cls, enum MHD_RequestTerminationCode term ) {
   struct HTTP* http = (struct HTTP*)cls;
   
   struct HTTP_connection_data* con_data = NULL;
   if( con_cls ) {
      con_data = (struct HTTP_connection_data*)(*con_cls);
   }
   
   if( http->HTTP_cleanup && con_data) {
      (*http->HTTP_cleanup)( connection, con_data->cls, term );
      con_data->cls = NULL;
   }
   if( con_data ) {
      if( con_data->pp ) {
         MHD_destroy_post_processor( con_data->pp );
      }
      if( con_data->resp ) {
         wish_free_HTTP_response( con_data->resp );
         free( con_data->resp );
      }
      free( con_data );
   }
}

// set fields in an HTTP structure
int wish_HTTP_init( struct HTTP* http, struct wish_state* state, struct HTTP_user_entry** users ) {
   memset( http, 0, sizeof(struct HTTP) );
   http->state = state;
   http->users = users;
   http->http_daemon = NULL;
   http->authentication_mode = 0;
   return 0;
}


// start the HTTP thread
int wish_start_HTTP( struct HTTP* http ) {
   char* server_key = NULL;
   char* server_cert = NULL;
   
   size_t server_key_len = 0;
   size_t server_cert_len = 0;
   
   wish_state_rlock( http->state );
   
   struct wish_conf* conf = &http->state->conf;
   char* server_key_path = NULL;
   char* server_cert_path = NULL;
   
   if( conf->server_key )
      server_key_path = strdup( conf->server_key );
   
   if( conf->server_cert )
      server_cert_path = strdup( conf->server_cert );
   
   int http_portnum = conf->http_portnum;
   
   wish_state_unlock( http->state );
   
   if( server_key_path && server_cert_path ) {
      server_key = wish_load_file( server_key_path, &server_key_len );
      server_cert = wish_load_file( server_cert_path, &server_cert_len );
      
      if( !server_key ) {
         errorf("Could not read server private key %s\n", server_key_path );
      }
      if( !server_cert ) {
         errorf("Could not read server certificate %s\n", server_cert_path );
      }
   }
   
   if( server_cert && server_key ) {
      // SSL enabled
      http->http_daemon = MHD_start_daemon( MHD_USE_SELECT_INTERNALLY, http_portnum, NULL, NULL, &wish_HTTP_connection_handler, http, 
                                            MHD_OPTION_HTTPS_MEM_KEY, server_key, 
                                            MHD_OPTION_HTTPS_MEM_CERT, server_cert, 
                                            MHD_OPTION_THREAD_POOL_SIZE, 4,
                                            MHD_OPTION_NOTIFY_COMPLETED, wish_HTTP_cleanup, http,
                                            MHD_OPTION_END );
   
      if( http->http_daemon )
         dbprintf("Started HTTP server with SSL enabled (cert = %s, pkey = %s)\n", server_key_path, server_cert_path);
   }
   else {
      // SSL disabled
      http->http_daemon = MHD_start_daemon( MHD_USE_SELECT_INTERNALLY, http_portnum, NULL, NULL, &wish_HTTP_connection_handler, http, 
                                            MHD_OPTION_THREAD_POOL_SIZE, 4,
                                            MHD_OPTION_NOTIFY_COMPLETED, wish_HTTP_cleanup, http,
                                            MHD_OPTION_END );
      
      if( http->http_daemon )
         dbprintf("%s", "Started HTTP server\n");
   }
   
   if( server_key_path )
      free( server_key_path );
   
   if( server_cert_path )
      free( server_cert_path );
   
   if( http->http_daemon == NULL )
      return -1;
   
   return 0;
}

// stop the HTTP thread
int wish_stop_HTTP( struct HTTP* http ) {
   MHD_stop_daemon( http->http_daemon );
   http->http_daemon = NULL;
   return 0;
}


// parse a secrets file
struct HTTP_user_entry** wish_parse_secrets_file( char* path ) {
   FILE* passwd_file = fopen( path, "r" );
   if( passwd_file == NULL ) {
      errorf("Could not read password file %s, errno = %d\n", path, -errno );
      return NULL;
   }
   
   // format: UID:username:password_hash:public_url\n
   char* buf = NULL;
   size_t len = 0;
   vector<struct HTTP_user_entry*> user_ents;
   int line_num = 0;
   
   while( true ) {
      ssize_t sz = getline( &buf, &len, passwd_file );
      line_num++;
      
      if( sz < 0 ) {
         // out of lines
         break;
      }
      if( buf == NULL ) {
         // error
         errorf("Error reading password file %s\n", path );
         return NULL;
      }
      
      // parse the line
      char* save_buf = NULL;
      char* uid_str = strtok_r( buf, ":", &save_buf );
      char* username_str = strtok_r( NULL, ":", &save_buf );
      char* password_hash = strtok_r( NULL, "\n", &save_buf );
      
      // sanity check
      if( uid_str == NULL || username_str == NULL || password_hash == NULL ) {
         errorf("Could not read password file %s: invalid line %d\n", path, line_num );
         return NULL;
      }
      
      // sanity check on UID
      uid_t uid = strtol( uid_str, NULL, 10 );
      if( uid == 0 ) {
         errorf("Could not read password file %s: invalid UID '%s' at line %d\n", path, uid_str, line_num );
         return NULL;
      }
      
      // save the data
      struct HTTP_user_entry* uent = (struct HTTP_user_entry*)calloc( sizeof(struct HTTP_user_entry), 1 );
      uent->uid = uid;
      uent->username = strdup( username_str );
      uent->password_hash = strdup( password_hash );
      
      dbprintf("wish_parse_secrets_file: %d %s %s\n", uid, username_str, password_hash);

      user_ents.push_back( uent );
   }
   
   // convert vector to NULL-terminated list
   struct HTTP_user_entry** ret = (struct HTTP_user_entry**)calloc( sizeof(struct HTTP_user_entry*) * (user_ents.size() + 1), 1 );
   for( unsigned int i = 0; i < user_ents.size(); i++ ) {
      ret[i] = user_ents.at(i);
   }
   
   return ret;
}


