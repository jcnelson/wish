// HTTP server implementation
// *** BORROWED FROM libsyndicate.h ***
// Original author: Jude Nelson

#ifndef _HTTP_H_
#define _HTTP_H_

#include <curl/curl.h>

#include "libwish.h"
#include "microhttpd.h"

// types of HTTP responses
#define HTTP_RESPONSE_RAM              1
#define HTTP_RESPONSE_FILE             2

// HTTP server user account entry
struct HTTP_user_entry {
   uid_t uid;  
   char* username;
   char* password_hash;
};

// HTTP handler args
struct HTTP_handler_args {
   struct wish_state* state;
   struct HTTP_user_entry** users;
};


// HTTP headers
struct HTTP_header {
   char* header;
   char* value;
};

// HTTP cgi argument
struct HTTP_CGI_arg {
   char* name;
   char* value;
};

// download buffer
struct HTTP_download_buf {
   ssize_t len;         // amount of data
   ssize_t data_len;    // size of data (if data was preallocated)
   char* data;          // NOT null-terminated
};

// HTTP file response
struct HTTP_file_response {
   FILE* f;
   off_t offset;
   size_t size;
};

// HTTP response (to be populated by handlers)
struct HTTP_response {
   int type;
   int status;
   char* mimetype;
   struct HTTP_header** headers;
   union {
      struct HTTP_download_buf   ram_data;      // reply is in RAM
      struct HTTP_file_response  file_data;     // reply is on disk
   };
};

// HTTP connection data
struct HTTP_connection_data {
   struct MHD_PostProcessor* pp;
   struct HTTP_user_entry* user;
   struct HTTP_response* resp;
   
   int status;
   int mode;
   
   void* cls;           // user-supplied closure
};

// HTTP callbacks and control code
struct HTTP {
   struct HTTP_user_entry** users;
   struct wish_state* state;
   int authentication_mode;
   struct MHD_Daemon* http_daemon;
   
   void*                     (*HTTP_connect)( struct HTTP* http, int mode, struct HTTP_user_entry* uent );
   struct HTTP_response*     (*HTTP_HEAD_handler)( struct HTTP* http, struct HTTP_user_entry* uent, void* cls, char* url, char* version, struct HTTP_header** headers );
   struct HTTP_response*     (*HTTP_GET_handler)( struct HTTP* http, struct HTTP_user_entry* uent, void* cls, char* url, char* version, struct HTTP_header** headers );
   int                       (*HTTP_POST_handler)( void *coninfo_cls, enum MHD_ValueKind kind, 
                                                   char const *key,
                                                   char const *filename, char const *content_type,
                                                   char const *transfer_encoding, char const *data, 
                                                   uint64_t off, size_t size);
   void                      (*HTTP_cleanup)(struct MHD_Connection *connection, void *con_cls, enum MHD_RequestTerminationCode term);
};


// HTTP server methods
int wish_create_HTTP_response_ram( struct HTTP_response* resp, char const* mimetype, int status, char* data, int len );
int wish_create_HTTP_response_disk( struct HTTP_response* resp, char const* mimetype, int status, FILE* file, off_t offset, size_t size );
void wish_free_HTTP_response( struct HTTP_response* resp );
void* wish_cls_get( void* cls );
void wish_cls_set_status( void* cls, int status );
int wish_HTTP_init( struct HTTP* http, struct wish_state* state, struct HTTP_user_entry** users );
int wish_start_HTTP( struct HTTP* http );
int wish_stop_HTTP( struct HTTP* http );
void wish_create_HTTP_header( struct HTTP_header* header, char const* h, char const* value );
void wish_create_HTTP_CGI_arg( struct HTTP_header* header, char const* n, char const* value );
void wish_free_HTTP_header( struct HTTP_header* header );
void wish_free_download_buf( struct HTTP_download_buf* buf );
struct HTTP_CGI_arg** wish_parse_cgi_args( char* _url );
struct HTTP_user_entry** wish_parse_secrets_file( char* path );

// authentication (can be OR'ed together)
#define HTTP_AUTHENTICATE_READ        1
#define HTTP_AUTHENTICATE_WRITE       2
#define HTTP_AUTHENTICATE_READWRITE   3

// mode for connection data
#define HTTP_GET      0
#define HTTP_POST     1
#define HTTP_UNKNOWN  -1

#endif
