// packet defining a job to be forwarded

#ifndef _JOB_PACKET_H_
#define _JOB_PACKET_H_

#include "libwish.h"

#define PACKET_TYPE_JOB    123

#define JOB_DETACHED    0x1      // don't need to join with process
#define JOB_USE_FILE    0x4      // the command text refers to a file on the origin to be downloaded and executed

#define JOB_WISH_ORIGIN 0x2      // job came from a WISH daemon, not a client. 
                                 // if this is NOT set (i.e. the wish_job_packet came
                                 // from a client), then you don't need to set
                                 // wish_job_packet.visited, wish_job_packet.visited_len, or 
                                 // wish_job_packet.stdin_url.

// job packet payload
struct wish_job_packet {
   uint64_t nid_dest;         // destination NID
   uint64_t nid_src;          // origin NID
   uint32_t ttl;              // packet TTL--how many times this packet can be forwarded
   uint32_t visited_len;      // length of the list of nodes this packet has visited besides the origin node
   uint32_t flags;            // job options
   uint64_t gpid;             // the global PID of the process
   time_t timeout;            // maximum amount of time this process is allowed to run, in seconds (-1 for infinite)
   
   char* cmd_text;            // shell command text
   char* stdin_url;           // stdin url on the origin host
   struct sockaddr_storage* visited;  // list of sockaddrs of nodes this packet has passed through
   int origin_http_portnum;   // port number of origin's HTTP server
   
   // used locally for routing stdout
   uint32_t umask;
   uint32_t owner;
   uint32_t group;
   char* stdout_path;
   char* stderr_path;
};

// make a job packet
void wish_init_job_packet( struct wish_state* state, struct wish_job_packet* pkt, uint64_t nid, uint32_t ttl, struct sockaddr_storage* visited, int num_visited, char* cmd, char* stdin_url, uint32_t flags, time_t timeout, int origin_http_portnum );
void wish_init_job_packet_client( struct wish_state* state, struct wish_job_packet* pkt, uint64_t gpid, uint64_t nid, uint32_t ttl, char* cmd, char* stdin, char* stdout, char* stderr, uid_t owner, gid_t group, int umask, uint32_t flags, time_t timeout );

// pack a job packet.
int wish_pack_job_packet( struct wish_state* state, struct wish_packet* wp, struct wish_job_packet* pkt );

// unpack a job packet
int wish_unpack_job_packet( struct wish_state* state, struct wish_packet* wp, struct wish_job_packet* pkt );

// free a job packet
int wish_free_job_packet( struct wish_job_packet* pkt );

#endif