#include "job_packet.h"

static int random_fd = -1;

// initialize a job packet.
// duplicate everything
void wish_init_job_packet( struct wish_state* state, struct wish_job_packet* pkt, uint64_t nid, uint32_t ttl, struct sockaddr_storage* visited, int num_visited, char* cmd, char* stdin_url, uint32_t flags, time_t timeout, int origin_http_portnum ) {
   memset( pkt, 0, sizeof(wish_job_packet) );
   
   pkt->ttl = ttl;
   
   if( visited != NULL ) {
      pkt->visited = (struct sockaddr_storage*)calloc( sizeof(struct sockaddr_storage) * num_visited, 1 );
      memcpy( pkt->visited, visited, sizeof(struct sockaddr_storage) * num_visited );
   }
   else {
      pkt->visited = NULL;
   }
   
   pkt->visited_len = num_visited;
   
   if( stdin_url )
      pkt->stdin_url = strdup( stdin_url );
   else
      pkt->stdin_url = strdup( "" );
   
   pkt->cmd_text = strdup( cmd );
   pkt->flags = flags;
   pkt->nid_dest = nid;
   
   if( random_fd == -1 )
      random_fd = open( "/dev/urandom", O_RDONLY );
   
   pkt->gpid = 0;
   while( pkt->gpid == 0 ) {
      read( random_fd, &pkt->gpid, sizeof(pkt->gpid) );
   }
   
   pkt->timeout = timeout;
   pkt->origin_http_portnum = origin_http_portnum;
   
   if( state ) {
      wish_state_rlock( state );
      pkt->nid_src = state->nid;
      wish_state_unlock( state );
   }
   else {
      pkt->nid_src = 0;
   }
}

// init for a client
void wish_init_job_packet_client( struct wish_state* state, struct wish_job_packet* pkt, uint64_t gpid, uint64_t nid, uint32_t ttl, char* cmd, char* stdin_path, char* stdout_path, char* stderr_path, uid_t owner, gid_t group, int umask, uint32_t flags, time_t timeout ) {
   wish_init_job_packet( state, pkt, nid, ttl, NULL, 0, cmd, stdin_path, flags & (~JOB_WISH_ORIGIN), timeout, 0 );
   
   if( gpid != 0 )
      pkt->gpid = gpid;
   
   if( stdout_path || stderr_path ) {
      if( stdout_path )
         pkt->stdout_path = strdup( stdout_path );
      if( stderr_path )
         pkt->stderr_path = strdup( stderr_path );
      
      pkt->umask = umask;
      pkt->owner = owner;
      pkt->group = group;
   }
}


// pack a job packet
// NOTE: this is NOT reentrant!  we temporarily modify pkt->visited
int wish_pack_job_packet( struct wish_state* state, struct wish_packet* wp, struct wish_job_packet* pkt ) {
   wish_init_header( state, &wp->hdr, PACKET_TYPE_JOB );
   
   // make a buffer to store this information
   size_t stdin_url_len = strlen(pkt->stdin_url);
   size_t cmd_len = strlen(pkt->cmd_text);
   size_t len = sizeof(struct wish_job_packet) +
                sizeof(struct sockaddr_storage) * pkt->visited_len + 
                sizeof(char) * (cmd_len + 1) +
                sizeof(char) * (stdin_url_len + 1);
                
   // if this is a client-created packet, then add the additional information such as stdout, stderr, and metadata
   if( !(pkt->flags & JOB_WISH_ORIGIN) ) {
      if( pkt->stdout_path )
         len += strlen( pkt->stdout_path ) + 1;
      else
         len += strlen( "" ) + 1;
      
      if( pkt->stderr_path )
         len += strlen( pkt->stderr_path ) + 1;
      else
         len += strlen( "" ) + 1;
   }
   
   uint8_t* packet_buf = (uint8_t*)calloc( len, 1 );
   
   // pack the data into packet_buf
   off_t offset = 0;
   wish_pack_ulong( packet_buf, &offset, pkt->nid_dest );
   wish_pack_ulong( packet_buf, &offset, pkt->nid_src );
   wish_pack_uint( packet_buf, &offset, pkt->ttl );
   wish_pack_uint( packet_buf, &offset, pkt->visited_len );
   wish_pack_uint( packet_buf, &offset, pkt->flags );
   wish_pack_long( packet_buf, &offset, pkt->timeout );
   wish_pack_int( packet_buf, &offset, pkt->origin_http_portnum );
   wish_pack_ulong( packet_buf, &offset, pkt->gpid );
   wish_pack_string( packet_buf, &offset, pkt->cmd_text );
   wish_pack_string( packet_buf, &offset, pkt->stdin_url );
   
   for( unsigned int i = 0; i < pkt->visited_len; i++ ) {
      wish_pack_sockaddr( packet_buf, &offset, &pkt->visited[i] );
   }
   
   if( !(pkt->flags & JOB_WISH_ORIGIN) ) {
      if( pkt->stdout_path ) {
         wish_pack_string( packet_buf, &offset, pkt->stdout_path );
      }
      else {
         wish_pack_string( packet_buf, &offset, (char*)"" );
      }
      
      if( pkt->stderr_path ) {
         wish_pack_string( packet_buf, &offset, pkt->stderr_path );
      }
      else {
         wish_pack_string( packet_buf, &offset, (char*)"" );
      }
      
      wish_pack_uint( packet_buf, &offset, pkt->umask );
      wish_pack_uint( packet_buf, &offset, pkt->owner );
      wish_pack_uint( packet_buf, &offset, pkt->group );
   }

   wish_init_packet_nocopy( wp, &wp->hdr, packet_buf, len );
   
   return 0;
}

// unpack a job packet
int wish_unpack_job_packet( struct wish_state* state, struct wish_packet* wp, struct wish_job_packet* pkt ) {
   
   memset( pkt, 0, sizeof(wish_job_packet) );
   
   // read the data out of packet_buf, in the order we put it there
   off_t offset = 0;
   pkt->nid_dest = wish_unpack_ulong( wp->payload, &offset );
   pkt->nid_src = wish_unpack_ulong( wp->payload, &offset );
   pkt->ttl = wish_unpack_uint( wp->payload, &offset );
   pkt->visited_len = wish_unpack_uint( wp->payload, &offset );
   pkt->flags = wish_unpack_uint( wp->payload, &offset );
   pkt->timeout = wish_unpack_long( wp->payload, &offset );
   pkt->origin_http_portnum = wish_unpack_int( wp->payload, &offset );
   pkt->gpid = wish_unpack_ulong( wp->payload, &offset );
   pkt->cmd_text = wish_unpack_string( wp->payload, &offset );
   pkt->stdin_url = wish_unpack_string( wp->payload, &offset );
   
   if( strlen(pkt->stdin_url) == 0 ) {
      free( pkt->stdin_url );
      pkt->stdin_url = NULL;
   }
    
   pkt->visited = (struct sockaddr_storage*)calloc( sizeof(struct sockaddr_storage) * pkt->visited_len, 1 );
   for( unsigned int i = 0; i < pkt->visited_len; i++ ) {
      struct sockaddr_storage* v = wish_unpack_sockaddr( wp->payload, &offset );
      memcpy( &pkt->visited[i], v, sizeof(struct sockaddr_storage) );
      free( v );
   }
   
   if( !(pkt->flags & JOB_WISH_ORIGIN) ) {
      char* str = wish_unpack_string( wp->payload, &offset );
      if( strlen(str) > 0 ) {
         pkt->stdout_path = str;
      }
      else {
         free( str );
      }
      
      str = wish_unpack_string( wp->payload, &offset );
      if( strlen(str) > 0 ) {
         pkt->stderr_path = str;
      }
      else {
         free( str );
      }
      
      pkt->umask = wish_unpack_uint( wp->payload, &offset );
      pkt->owner = wish_unpack_uint( wp->payload, &offset );
      pkt->group = wish_unpack_uint( wp->payload, &offset );
   }
   
   return 0;
}


// free a job packet
int wish_free_job_packet( struct wish_job_packet* pkt ) {
   if( pkt->visited ) {
      free( pkt->visited );
      pkt->visited = NULL;
   }
   if( pkt->cmd_text ) {
      free( pkt->cmd_text );
      pkt->cmd_text = NULL;
   }
   if( pkt->stdin_url ) {
      free( pkt->stdin_url );
      pkt->stdin_url = NULL;
   }
   return 0;
}

