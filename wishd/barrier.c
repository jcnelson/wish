#include "barrier.h"

// read/write lock to the barrier list
static pthread_rwlock_t barrier_rwlock;

// set of barriers in operation
typedef vector<struct wish_barrier_status*> BarrierList;
static BarrierList barriers;

// read-lock the barriers
static int barriers_rlock() { return pthread_rwlock_rdlock( &barrier_rwlock ); }

// write-lock the barriers
static int barriers_wlock() { return pthread_rwlock_wrlock( &barrier_rwlock ); }

// unlock the barriers
static int barriers_unlock() { return pthread_rwlock_unlock( &barrier_rwlock ); }

// initialize a barrier status structure
static int barrier_init_status( struct wish_state* state, struct wish_barrier_status* status, struct barrier_packet* bpkt ) {
   status->b_info = bpkt;
   status->proc_cons = new RendezvousList();
   status->expire = wish_time_millis() + bpkt->timeout;
   return 0;
}

// free a barrier status structure's memory
static int barrier_free_status( struct wish_state* state, struct wish_barrier_status* status ) {
   if( status->b_info ) {
      wish_free_barrier_packet( status->b_info );
      free( status->b_info );
      status->b_info = NULL;
   }
   if( status->proc_cons ) {
      for( RendezvousList::iterator itr = status->proc_cons->begin(); itr != status->proc_cons->end(); itr++ ) {
         if( itr->second != NULL ) {
            wish_disconnect( state, itr->second );
            free( itr->second );
            itr->second = NULL;
         }
      }
      
      delete status->proc_cons;
      status->proc_cons = NULL;
   }
   
   return 0;
}


// set up the barrier system
int barrier_init( struct wish_state* state ) {
   pthread_rwlock_init( &barrier_rwlock, NULL );
   
   return 0;
}


// clean up the barrier system
int barrier_shutdown( struct wish_state* state ) {
   barriers_wlock();
   
   for( BarrierList::size_type i = 0; i < barriers.size(); i++ ) {
      barrier_free_status( state, barriers[i] );
      free( barriers[i] );
      barriers[i] = NULL;
   }
   
   barriers_unlock();
   
   pthread_rwlock_destroy( &barrier_rwlock );
   
   return 0;
}


// create a barrier
int barrier_add( struct wish_state* state, struct wish_connection* con, struct barrier_packet* bpkt ) {
   struct wish_barrier_status* status = (struct wish_barrier_status*)calloc( sizeof(struct wish_barrier_status), 1 );
   barrier_init_status( state, status, bpkt );
   
   status->proc_cons->push_back( Rendezvous(bpkt->gpid_self, con) );
   
   //barriers_wlock();
   barriers.push_back( status );
   //barriers_unlock();
   
   return 0;
}

// release a barrier--tell all processes to continue
int barrier_release( struct wish_state* state, struct wish_barrier_status* status ) {
   struct barrier_packet ack;
   struct wish_packet pkt;
   
   wish_init_barrier_packet( state, &ack, 0, 0, status->b_info->num_procs, status->b_info->gpids );
   wish_pack_barrier_packet( state, &pkt, &ack );
   
   int worst_rc = 0;
   
   for( RendezvousList::iterator itr = status->proc_cons->begin(); itr != status->proc_cons->end(); itr++ ) {
      int rc = wish_write_packet( state, itr->second, &pkt );
      if( rc != 0 ) {
         // failed to write packet
         errorf("barrier_release: rc = %d when sending barrier ACK\n", rc );
         worst_rc = min( -errno, worst_rc );
      }
   }
   
   wish_free_barrier_packet( &ack );
   wish_free_packet( &pkt );
   
   // release memory if we succeeded
   if( worst_rc == 0 )
      barrier_free_status( state, status );
   
   return worst_rc;
}


// process a barrier (make a new barrier or acknowledge an existing one)
int barrier_process( struct wish_state* state, struct wish_connection* con, struct barrier_packet* bpkt ) {
   
   int rc = 0;
   bool processed = false;
   
   barriers_wlock();
   int dead = -1;
   
   // before we begin, remove stale barriers
   barrier_purge_old( state );
   
   for( unsigned int i = 0; i < barriers.size(); i++ ) {
      struct wish_barrier_status* status = barriers[i];
      
      if( wish_barrier_equal( status->b_info, bpkt ) ) {
         // This incoming barrier packet may be an acknowledgement for an existing barrier.
         // Verify that it has not already acknowledged this barrier.
         bool acked = false;
         for( unsigned int j = 0; j < status->proc_cons->size(); j++ ) {
            if( (*status->proc_cons)[j].first == bpkt->gpid_self ) {
               // this barrier has already been acknowledged
               acked = true;
               break;
            }
         }
         
         if( acked ) {
            // already acknowledged--keep going
            continue;
         }
         else {
            // unacknowledged--ack it (NOTE: all values of itr->proc_cons->first will be unique)
            status->proc_cons->push_back( Rendezvous( bpkt->gpid_self, con ) );
            
            // has this barrier been fully acknowledged?
            if( status->proc_cons->size() == status->b_info->num_procs ) {
               // yup--release it
               rc = barrier_release( state, status );
               if( rc != 0 ) {
                  // failed to release the barrier.
                  // TODO: more elegant error handling?
                  errorf("barrier_process: failed to fully release barrier of %lu\n", status->b_info->gpid_self );
                  barrier_free_status( state, status );
               }
               dead = i;
            }
            
            processed = true;
            break;
         }
      }
   }
   
   if( !processed ) {
      // is this the special case where there is one GPID and it matches gpid_self?
      if( bpkt->num_procs == 1 && bpkt->gpid_self == bpkt->gpids[0] ) {
         struct barrier_packet ack;
         struct wish_packet wpkt;
         
         wish_init_barrier_packet( state, &ack, 0, 0, bpkt->num_procs, bpkt->gpids );
         wish_pack_barrier_packet( state, &wpkt, &ack );
         
         rc = wish_write_packet( state, con, &wpkt );
         
         wish_free_barrier_packet( &ack );
         wish_free_packet( &wpkt );
         
         if( rc != 0 ) {
            errorf("barrier_process: failed to fully release barrier for %lu\n", bpkt->gpid_self);
         }
      }
      else {
         // no barrier exists.  Create a new one
         rc = barrier_add( state, con, bpkt );
      }
   }
   
   if( dead != -1 ) {
      barriers.erase( barriers.begin() + dead );
   }
   
   barriers_unlock();
   return rc;
}


// remove stale barriers
int barrier_purge_old( struct wish_state* state ) {
   uint64_t now = wish_time_millis();
   
   if( barriers.size() > 0 ) {
      for( BarrierList::iterator itr = barriers.begin() + (barriers.size() - 1); itr != barriers.begin(); itr-- ) {
         if( (*itr)->expire < now ) {
            // it's expired
            barrier_free_status( state, *itr );
            barriers.erase( itr );
         }
      }
   }
   return 0;
}

