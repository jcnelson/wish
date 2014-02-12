/**
 * libwish.h
 * 
 * Common data structures and library methods to be shared by
 * most/all libwish components.
 * 
 * Author: Jude Nelson (jcnelson@cs.princeton.edu)
 */

#ifndef _LIBWISH_H_
#define _LIBWISH_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <sys/sysinfo.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <openssl/sha.h>
#include <sys/stat.h>
#include <locale>
#include <vector>
#include <curl/curl.h>
#include <fcntl.h>

#include "packets.h"

using namespace std;

/********** BORROWED FROM libsyndicate.h ***********
 * Original author: Jude Nelson
 ***************************************************/
extern int _DEBUG;

#define WHERESTR "[%u %s:%u] "
#define WHEREARG getpid(), __FILE__, __LINE__

#define dbprintf( format, ... ) if( _DEBUG ) do { printf( WHERESTR format, WHEREARG, __VA_ARGS__ ); fflush(stdout); } while(0)
#define errorf( format, ... ) if( _DEBUG) do { fprintf(stderr, WHERESTR format, WHEREARG, __VA_ARGS__); fflush(stderr); } while(0)


/***************************************************/


#define WISH_DEFAULT_CONFIG     "/etc/wish/wish.conf"

#define WISH_HTTP_TEMP     "TEMP"
#define WISH_HTTP_GETENV   "GETENV"
#define WISH_HTTP_SETENV   "SETENV"
#define WISH_HTTP_FILE     "FILE"
#define WISH_HTTP_TASET    "TASET"

// environment variables
#define WISH_ORIGIN_ENV "WISH_ORIGIN"
#define WISH_PORTNUM_ENV "WISH_PORTNUM"
#define WISH_HTTP_PORTNUM_ENV "WISH_HTTP_PORTNUM"
#define WISH_GPID_ENV   "WISH_GPID"

#define MAX(a,b) ((a) > (b) ? (a) : (b))

#define WISH_MAX_ENVAR_SIZE 65536

#define WISH_MAX_PACKET_SIZE 1048576      // 1 MB


// packet header.
// a client-created header will NOT have origin set (it will be zero'ed) and will NOT have uid set
struct wish_packet_header {
   // packet header information
   uint32_t type;                // which command is being issued
   uint32_t uid;                 // user ID of the issuer
   struct sockaddr_storage origin;   // sockaddr structure describing originating host
   uint32_t payload_len;         // how long the payload is
};

// (default) wish command packet
struct wish_packet {
   struct wish_packet_header hdr;       // the packet header
   uint8_t* payload;                    // packet's payload
};

// host entry
struct wish_hostent {
   char* hostname;
   int portnum;
};

// wish configuration structure
struct wish_conf {
   // filled in by the config file
   int portnum;                  // port that the daemon listens on
   int connect_timeout;          // connection timeout (in milliseconds)
   int daemon_backlog;           // how many connections should we buffer for the daemon?
   uint32_t uid;                 // UID of the person running this piece of software
   char* files_root;             // topmost directory that can be exposed for reading/writing files on this host
   int status_memory;            // how many heartbeat packets per host do we need?
   int64_t heartbeat_interval;   // how often do we send a heartbeat (in milliseconds)?
   char* shell;                  // shell command to parse user commands
   int shell_argc;               // number of arguments in shell_argv
   char** shell_argv;            // shell command-line arguments
   char* tmp_dir;                // directory for temporary files
   char* server_key;             // path to server's x509 key file
   char* server_cert;            // path to server's x509 cert file
   int http_portnum;             // HTTP server port number
   char* http_secrets;           // path to HTTP secrets file
   bool use_https;               // whether or not to use HTTPS
   time_t job_timeout;           // default process timeout
   
   struct wish_hostent** initial_peers;         // initial peers
};

// wish run-time state structure
struct wish_state {
   struct wish_conf conf;       // system configuration
   
   // filled in at runtime
   struct addrinfo* addr;       // address of this host
   int daemon_sock;             // server socket to listen for incoming connections
   struct HTTP* http;           // HTTP server information
   vector<char*>* fs_invisible;   // list of directories in conf.files_root that are off-limits
   uint64_t nid;                // our nid
   char* hostname;              // our looked-up hostname
   vector<struct wish_connection*>* client_cons;    // connection to our clients
   
   // read/write lock to access this structure
   pthread_rwlock_t lock;
};


// wish remote daemon connection.
// NOTE: it is assumed that a connection is private to a thread, so no
// synchronization primitives have been defined for it.
struct wish_connection {
   struct addrinfo* addr;
   struct wish_packet* last_packet_recved;    // last packet received (used in wish_read_packet for non-blocking I/O)
   
   bool have_header;                          // has the header been received?
   struct wish_packet_header tmp_hdr;         // temporary buffer to store a header
   ssize_t num_read;                          // number of bytes read so far (if !have_header, this applies to the header; otherwise the payload)
   
   int soc;
};

// http information
struct wish_HTTP_info {
   long size;
   int status;
   char* mimetype;
};

// http ram buffer
struct wish_HTTP_buf {
   ssize_t size;
   off_t offset;
   char* data;
};

// configuration keys
#define COMMENT_KEY              '#'
#define PORTNUM_KEY              "PORTNUM"
#define CONNECT_TIMEOUT_KEY      "CONNECT_TIMEOUT"
#define DAEMON_BACKLOG_KEY       "DAEMON_BACKLOG"
#define USER_ID_KEY              "USER_ID"
#define FILES_ROOT_KEY           "FILES_ROOT"
#define STATUS_MEMORY_KEY        "STATUS_MEMORY"
#define HEARTBEAT_INTERVAL_KEY   "HEARTBEAT_INTERVAL"
#define PEER_KEY                 "PEER"
#define SHELL_KEY                "SHELL"
#define SHELL_ARGS_KEY           "SHELL_ARGS"
#define TMP_DIR_KEY              "TEMP_DIR"
#define SSL_KEY_KEY              "SSL_KEY"
#define SSL_CERT_KEY             "SSL_CERT"
#define HTTP_PORTNUM_KEY         "HTTP_PORTNUM"
#define JOB_TIMEOUT_KEY          "JOB_TIMEOUT"
#define USE_HTTPS_KEY            "USE_HTTPS"
#define DEBUG_KEY                "DEBUG"

// parse configuration file
// return 0 on success, -errno on failure
int wish_read_conf( char const* path, struct wish_conf* conf );

// access control on conf and state.  Basically front-ends to pthread_rwlock_[rd|wr|un]lock().
int wish_state_rlock( struct wish_state* state );
int wish_state_wlock( struct wish_state* state );
int wish_state_unlock( struct wish_state* state );

// library startup/shutdown.  Populate a state structure with runtime information
// return 0 on success, -errno on failure
int wish_init( struct wish_state* state );

// ensure that the directories wish will be using exist.  Create them if necessary.
int wish_init_dirs( struct wish_state* state );

// shut down and free memory.
// NOTE: state must be write-locked before calling this!
int wish_shutdown( struct wish_state* state );

// initialize the daemon server, and put the daemon's server socket in state.
// return the daemon's server socket on success, or a negative errno on failure 
// (in which case the value of the server socket will be unchanged in state).
int wish_init_daemon( struct wish_state* state );

// initialize a packet header
// return 0 on success; negative on error
int wish_init_header( struct wish_state* state, struct wish_packet_header* hdr, uint32_t type );

// initialize a packet
int wish_init_packet( struct wish_packet* wp, struct wish_packet_header* hdr, uint8_t* payload, uint32_t len );
int wish_init_packet_nocopy( struct wish_packet* wp, struct wish_packet_header* hdr, uint8_t* payload, uint32_t len );

// free a packet's data
int wish_free_packet( struct wish_packet* wp );

// accept an inbound connection.
// return 0 on success and populate con; return -errno on failure
int wish_accept( struct wish_state* state, struct wish_connection* con );

// connect to another daemon, populating the given con.
// return 0 success, or a negative errno on failure
int wish_connect( struct wish_state* state, struct wish_connection* con, char const* hostname, int portnum );

// disconnect from a daemon, freeing the con's internal data
// return 0 on success, or a negative errno on failure
int wish_disconnect( struct wish_state* state, struct wish_connection* con );

// set the receive timeout of a socket.
// return 0 on success; negative errno on error
int wish_recv_timeout( struct wish_state* state, struct wish_connection* con, uint64_t timeout_ms );

// read a (default) packet from a socket.
// return 0 on success, -errno on failure
int wish_read_packet( struct wish_state* state, struct wish_connection* con, struct wish_packet* wp );
int wish_read_packet_noblock( struct wish_state* state, struct wish_connection* con, struct wish_packet* wp );

// clear a connection
int wish_clear_connection( struct wish_state* state, struct wish_connection* con );

// write a (default) packet to a socket
// return 0 on success, -errno on failure
int wish_write_packet( struct wish_state* state, struct wish_connection* con, struct wish_packet* wp );

// clone a connection, which the caller can close/free safely
int wish_connection_clone( struct wish_state* state, struct wish_connection* old, struct wish_connection* next );

// free a connection (but don't close it)
int wish_connection_free( struct wish_state* state, struct wish_connection* con );

// pack data into a buffer, converting it to network byte order
void wish_pack_char( uint8_t* buf, off_t* offset, char value );
void wish_pack_byte( uint8_t* buf, off_t* offset, int8_t value );
void wish_pack_ubyte( uint8_t* buf, off_t* offset, uint8_t value );
void wish_pack_short( uint8_t* buf, off_t* offset, int16_t value );
void wish_pack_ushort( uint8_t* buf, off_t* offset, uint16_t value );
void wish_pack_int( uint8_t* buf, off_t* offset, int32_t value );
void wish_pack_uint( uint8_t* buf, off_t* offset, uint32_t value );
void wish_pack_long( uint8_t* buf, off_t* offset, int64_t value );
void wish_pack_ulong( uint8_t* buf, off_t* offset, uint64_t value );
void wish_pack_string( uint8_t* buf, off_t* offset, char* value );
void wish_pack_sockaddr( uint8_t* buf, off_t* offset, struct sockaddr_storage* value );

// unpack data from a buffer, converting it to host byte order
char wish_unpack_char( uint8_t* buf, off_t* offset );
int8_t wish_unpack_byte( uint8_t* buf, off_t* offset );
uint8_t wish_unpack_ubyte( uint8_t* buf, off_t* offset );
int16_t wish_unpack_short( uint8_t* buf, off_t* offset );
uint16_t wish_unpack_ushort( uint8_t* buf, off_t* offset );
int32_t wish_unpack_int( uint8_t* buf, off_t* offset );
uint32_t wish_unpack_uint( uint8_t* buf, off_t* offset );
int64_t wish_unpack_long( uint8_t* buf, off_t* offset );
uint64_t wish_unpack_ulong( uint8_t* buf, off_t* offset );
char* wish_unpack_string( uint8_t* buf, off_t* offset );
struct sockaddr_storage* wish_unpack_sockaddr( uint8_t* buf, off_t* offset );

// load a file into RAM
char* wish_load_file( char* path, size_t* size );

// host to NID
uint64_t wish_host_nid( char const* hostname );

// get a file.  pass -1 for fd if you don't want to save anything to disk, but instead fill out resp.
int wish_HTTP_download_file( struct wish_state* state, struct wish_HTTP_info* resp, char const* url, char const* username, char const* password, int fd );

// get a file.  fill its data into buf, which much be pre-allocated
int wish_HTTP_download_ram( struct wish_state* state, struct wish_HTTP_info* resp, char const* url, char const* username, char const* password, struct wish_HTTP_buf* buf );

// allocate an HTTP buffer
int wish_make_HTTP_buf( struct wish_HTTP_buf* buf, ssize_t size );

// free an HTTP buffer
int wish_free_HTTP_buf( struct wish_HTTP_buf* buf );

// free HTTP info
int wish_free_HTTP_info( struct wish_HTTP_info* info );

// utility functions
// BORROWED FROM libsyndicate
char* fullpath( char const* root, char const* path, char* dest );
int mkdirs( char const* dirp );
char* wish_basename( char const* path, char* dest );

uint64_t wish_time_millis();

#endif