#include "heartbeat_packet.h"

static pthread_mutex_t id_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t next_id = 1;

// make a heartbeat packet, by reading the state of the system
static int wish_init_heartbeat_packet_impl( struct wish_state* state, struct wish_heartbeat_packet* h ) {
   memset( h, 0, sizeof(struct wish_heartbeat_packet) );
   
   struct sysinfo sys;
   int rc = sysinfo( &sys );
   if( rc < 0 ) {
      return -errno;
   }
   
   wish_state_rlock( state );
   struct statfs fs;
   rc = statfs( state->conf.files_root, &fs );
   wish_state_unlock( state );
   
   if( rc < 0 ) {
      return -errno;
   }
   
   rc = gettimeofday( &h->sendtime, NULL );
   if( rc < 0 ) {
      return -errno;
   }
   
   h->loads[0] = sys.loads[0];
   h->loads[1] = sys.loads[1];
   h->loads[2] = sys.loads[2];
   
   h->ram_total = sys.totalram;
   h->ram_free = sys.freeram + sys.bufferram;
   h->disk_total = fs.f_blocks * fs.f_bsize;
   h->disk_free = fs.f_bavail * fs.f_bsize;
   
   return 0;
}

// make a heartbeat packet, with an ID
int wish_init_heartbeat_packet( struct wish_state* state, struct wish_heartbeat_packet* h ) {
   int rc = wish_init_heartbeat_packet_impl( state, h );
   if( rc != 0 )
      return rc;
   
   pthread_mutex_lock( &id_mutex );
   h->id = next_id;
   next_id++;
   pthread_mutex_unlock( &id_mutex );
   
   return 0;
}

// make an acknowledgement to a heartbeat packet (preserve the ID)
int wish_init_heartbeat_packet_ack( struct wish_state* state, struct wish_heartbeat_packet* ack, struct wish_heartbeat_packet* original ) {
   int rc = wish_init_heartbeat_packet_impl( state, ack );
   if( rc != 0 )
      return rc;
   
   ack->id = original->id;
   return 0;
}

// initialize a wish nget packet
int wish_init_nget_packet( struct wish_state* state, struct wish_nget_packet* npkt, uint64_t rank, uint32_t props ) {
   npkt->rank = rank;
   npkt->props = props;
   return 0;
}

// pack a heartbeat packet
int wish_pack_heartbeat_packet( struct wish_state* state, struct wish_packet* wp, struct wish_heartbeat_packet* h ) {
   wish_init_header( state, &wp->hdr, PACKET_TYPE_HEARTBEAT );
   
   uint8_t* packet_buf = (uint8_t*)calloc( sizeof(struct wish_heartbeat_packet), 1 );
   
   off_t offset = 0;
   
   wish_pack_uint( packet_buf, &offset, h->id );
   wish_pack_ulong( packet_buf, &offset, h->loads[0] );
   wish_pack_ulong( packet_buf, &offset, h->loads[1] );
   wish_pack_ulong( packet_buf, &offset, h->loads[2] );
   wish_pack_ulong( packet_buf, &offset, h->ram_total );
   wish_pack_ulong( packet_buf, &offset, h->ram_free );
   wish_pack_ulong( packet_buf, &offset, h->disk_total );
   wish_pack_ulong( packet_buf, &offset, h->disk_free );
   
   wish_init_packet_nocopy( wp, &wp->hdr, packet_buf, sizeof(struct wish_heartbeat_packet) );
   
   return 0;
}

// pack a nget packet
int wish_pack_nget_packet( struct wish_state* state, struct wish_packet* wp, struct wish_nget_packet* pkt ) {
   wish_init_header( state, &wp->hdr, PACKET_TYPE_NGET );
   
   uint8_t* packet_buf = (uint8_t*)calloc( sizeof(struct wish_nget_packet), 1 );
   
   off_t offset = 0;
   
   wish_pack_ulong( packet_buf, &offset, pkt->rank );
   wish_pack_uint( packet_buf, &offset, pkt->props );
   
   wish_init_packet_nocopy( wp, &wp->hdr, packet_buf, sizeof(struct wish_nget_packet) );
   return 0;
}

// unpack a heartbeat packet
int wish_unpack_heartbeat_packet( struct wish_state* state, struct wish_packet* wp, struct wish_heartbeat_packet* h ) {
   off_t offset = 0;
   
   h->id = wish_unpack_uint( wp->payload, &offset );
   h->loads[0] = wish_unpack_ulong( wp->payload, &offset );
   h->loads[1] = wish_unpack_ulong( wp->payload, &offset );
   h->loads[2] = wish_unpack_ulong( wp->payload, &offset );
   h->ram_total = wish_unpack_ulong( wp->payload, &offset );
   h->ram_free = wish_unpack_ulong( wp->payload, &offset );
   h->disk_total = wish_unpack_ulong( wp->payload, &offset );
   h->disk_free = wish_unpack_ulong( wp->payload, &offset );
   
   return 0;
}

// unpack an nget packet
int wish_unpack_nget_packet( struct wish_state* state, struct wish_packet* wp, struct wish_nget_packet* pkt ) {
   off_t offset = 0;
   
   pkt->rank = wish_unpack_ulong( wp->payload, &offset );
   pkt->props = wish_unpack_uint( wp->payload, &offset );
   
   return 0;
}
