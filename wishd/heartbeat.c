#include "heartbeat.h"

typedef map<long, struct wish_host_status*> HostHeartbeats;

static HostHeartbeats host_heartbeats;
static int _STATUS_MEMORY = 0;

// lock on host_heartbeats
static pthread_rwlock_t host_heartbeats_lock;

// heartbeat processing thread
static pthread_t host_heartbeat_thread;

void* heartbeat_thread(void* arg);

// rlock host_heartbeats
static int host_heartbeats_rlock(void) {
   return pthread_rwlock_rdlock( &host_heartbeats_lock );
}

// wlock host_heartbeats
static int host_heartbeats_wlock(void) {
   return pthread_rwlock_wrlock( &host_heartbeats_lock );
}

// unlock host_heartbeats
static int host_heartbeats_unlock(void) {
   return pthread_rwlock_unlock( &host_heartbeats_lock );
}


// initialize a host status
static int wish_host_status_init( struct wish_state* state, struct wish_host_status* status, struct wish_connection* con ) {
   
   if( con ) {
      char hostname[HOST_NAME_MAX+1];
      char portnum_buf[10];
      
      int rc = getnameinfo( con->addr->ai_addr, con->addr->ai_addrlen, hostname, HOST_NAME_MAX, portnum_buf, 10, NI_NUMERICHOST | NI_NUMERICSERV );
      if( rc != 0 )
         return rc;
      
      status->portnum = strtol( portnum_buf, NULL, 10 );
      status->hostname = strdup( hostname );
      status->nid = wish_host_nid( hostname );
      
      status->con = *con;
      wish_recv_timeout( state, &status->con, 0 );    // don't time out
   }
   else {
      status->con.soc = -1;
      status->con.addr = NULL;
   } 
   
   status->pending = new vector<struct wish_heartbeat_packet*>();
   status->heartbeats = new vector<struct wish_heartbeat_packet*>();
   
   return 0;
}


// initialize a host status
static int wish_host_status_init2( struct wish_state* state, struct wish_host_status* status, char* hostname, int portnum ) {
   
   status->portnum = portnum;
   status->hostname = strdup( hostname );
   status->nid = wish_host_nid( hostname );
   
   memset( &status->con, 0, sizeof(struct wish_connection) );
   status->con.soc = -1;
   
   status->pending = new vector<struct wish_heartbeat_packet*>();
   status->heartbeats = new vector<struct wish_heartbeat_packet*>();
   
   return 0;
}


// initialize heartbeat monitoring
int heartbeat_init( struct wish_state* state ) {
   pthread_rwlock_init( &host_heartbeats_lock, NULL );
   
   wish_state_rlock( state );
   _STATUS_MEMORY = state->conf.status_memory;
   
   // populate heartbeat table with initial peers
   host_heartbeats_wlock();
   for( int i = 0; state->conf.initial_peers[i] != NULL; i++ ) {
      struct wish_host_status* status = (struct wish_host_status*)calloc( sizeof(struct wish_host_status), 1 );
      
      wish_host_status_init2( state, status, state->conf.initial_peers[i]->hostname, state->conf.initial_peers[i]->portnum );
      
      // attempt to connect
      int rc = wish_connect( state, &status->con, status->hostname, status->portnum );
      if( rc == 0 ) {
         dbprintf("heartbeat_init: connected to %s on socket %d\n", status->hostname, status->con.soc );
         wish_recv_timeout( state, &status->con, 0 );    // don't time out
      }
      else {
         errorf("heartbeat_init: wish_connect on %s:%d rc = %d\n", status->hostname, status->portnum, rc );
      }
      
      host_heartbeats[ wish_host_nid(state->conf.initial_peers[i]->hostname) ] = status;
   }
   
   host_heartbeats_unlock();
   
   wish_state_unlock( state );
   
   int rc = pthread_create( &host_heartbeat_thread, NULL, heartbeat_thread, state );
   if( rc < 0 ) {
      errorf("heartbeat_init: pthread_create(heartbeat) rc = %d\n", rc );
      return -errno;
   }
   
   return 0;
}


// shut down heartbeat monitoring
int heartbeat_shutdown( struct wish_state* state ) {
   pthread_kill( host_heartbeat_thread, SIGKILL );
   pthread_join( host_heartbeat_thread, NULL );
   
   for( HostHeartbeats::iterator itr = host_heartbeats.begin(); itr != host_heartbeats.end(); itr++ ) {
      struct wish_host_status* hs = itr->second;
      
      for( vector<struct wish_heartbeat_packet*>::iterator itr2 = hs->pending->begin(); itr2 != hs->pending->end(); itr2++ ) {
         if( *itr2 ) {
            free( *itr2 );
            *itr2 = NULL;
         }
      }
      
      for( vector<struct wish_heartbeat_packet*>::iterator itr2 = hs->heartbeats->begin(); itr2 != hs->heartbeats->end(); itr2++ ) {
         if( *itr2 ) {
            free( *itr2 );
            *itr2 = NULL;
         }
      }
      
      delete hs->pending;
      delete hs->heartbeats;
      
      wish_disconnect( state, &hs->con );
      free( hs->hostname );
      free( hs );
      itr->second = NULL;
   }
   host_heartbeats.clear();
   
   pthread_rwlock_destroy( &host_heartbeats_lock );
   return 0;
}


// thread to send/receive heartbeats periodically
void* heartbeat_thread( void* arg ) {
   struct wish_state* state = (struct wish_state*)arg;
   
   wish_state_rlock( state );
   uint64_t interval = state->conf.heartbeat_interval;
   wish_state_unlock( state );
   
   int rc = 0;
   uint64_t last_write = -1;
   while( 1 ) {
      // first, attempt to repair any broken connections
      host_heartbeats_wlock();
      
      for( HostHeartbeats::iterator itr = host_heartbeats.begin(); itr != host_heartbeats.end(); itr++ ) {
         // verify that this connection is good
         struct timeval tv;
         socklen_t sz = sizeof(tv);
         rc = getsockopt( itr->second->con.soc, SOL_SOCKET, SO_RCVTIMEO, &tv, &sz );
         if( rc == -1 && errno == -EBADF ) {
            wish_disconnect( state, &itr->second->con );
         }         
      }
      
      fd_set rfds;
      FD_ZERO( &rfds );
      
      // add each connection to our rfd set
      int max_fd = -1;
      for( HostHeartbeats::iterator itr = host_heartbeats.begin(); itr != host_heartbeats.end(); itr++ ) {
         if( itr->second->con.soc > 0 ) {
            FD_SET( itr->second->con.soc, &rfds );
            if( itr->second->con.soc > max_fd )
               max_fd = itr->second->con.soc;
         }
      }
      
      struct timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = 5000;
      
      if( max_fd > 0 ) {
         int num_ready = select( max_fd + 1, &rfds, NULL, NULL, &tv );
         if( num_ready < 0 ) {
            // problem
            errorf("heartbeat_thread: errno = %d on select\n", -errno);
         }
         else {
            
            typedef pair<struct wish_connection*, struct wish_heartbeat_packet*> AckHandle;
            typedef vector<AckHandle> AckBuf;
            
            AckBuf to_ack;
            
            for( HostHeartbeats::iterator itr = host_heartbeats.begin(); itr != host_heartbeats.end(); itr++ ) {
               if( itr->second->con.soc < 0 )
                  continue;
               
               if( FD_ISSET( itr->second->con.soc, &rfds ) ) {
                  
                  // get the packet
                  struct wish_packet packet;
                  rc = wish_read_packet_noblock( state, &itr->second->con, &packet );
                  if( rc != 0 ) {
                     if( rc != -EAGAIN && rc != -EWOULDBLOCK ) {
                        errorf("heartbeat_thread: wish_read_packet rc = %d\n", rc );
                        wish_disconnect( state, &itr->second->con );
                     }
                     continue;
                  }
                  
                  // process the packet
                  rc = heartbeat_process( state, &itr->second->con, &packet );
                  if( rc < 0 ) {
                     errorf("heartbeat_thread: heartbeat_process rc = %d\n", rc );
                     wish_free_packet( &packet );
                     continue;
                  }
                  
                  // do we need to ack it?
                  else if( rc > 0 ) {
                     struct wish_heartbeat_packet* whp = (struct wish_heartbeat_packet*)calloc( sizeof(struct wish_heartbeat_packet), 1 );
                     wish_unpack_heartbeat_packet( state, &packet, whp );
                     
                     to_ack.push_back( AckHandle( &itr->second->con, whp ) );
                     
                     wish_free_packet( &packet );
                  }
                  else {
                     wish_free_packet( &packet );
                  }
               }
            }
            
            // send ack heartbeats to wish daemon instances that sent us a heartbeat we could use
            if( to_ack.size() > 0 ) {
               struct wish_heartbeat_packet* ack = (struct wish_heartbeat_packet*)calloc( sizeof(struct wish_heartbeat_packet), 1 );
               
               for( AckBuf::iterator itr = to_ack.begin(); itr != to_ack.end(); itr++ ) {
                  rc = wish_init_heartbeat_packet_ack( state, ack, itr->second );
                  if( rc != 0 ) {
                     errorf("heartbeat_thread: failed to create heartbeat, rc = %d\n", rc );
                     free( itr->second );
                     continue;
                  }
                  
                  struct wish_packet wp;
                  wish_pack_heartbeat_packet( state, &wp, ack );
                  
                  rc = wish_write_packet( state, itr->first, &wp );
                  if( rc != 0 ) {
                     errorf("heartbeat_thread: failed to send ACK, rc = %d\n", rc );
                  }
                  
                  wish_free_packet( &wp );
                  free( itr->second );
               }
               
               free( ack );
            }
            
            // send normal heartbeats to wish daemon instances we know about, but didn't receive a packet from
            uint64_t now = wish_time_millis();
            if( last_write + interval < now ) {
               last_write = now;
               
               struct wish_heartbeat_packet whp;
               rc = wish_init_heartbeat_packet( state, &whp );
               if( rc != 0 ) {
                  errorf("heartbeat_thread: failed to create a hearbeat packet, rc = %d\n", rc );
               }
               else {
                  struct wish_packet wp;
                  wish_pack_heartbeat_packet( state, &wp, &whp );
                  
                  for( HostHeartbeats::iterator itr = host_heartbeats.begin(); itr != host_heartbeats.end(); itr++ ) {
                     if( itr->second->con.soc < 0 )
                        continue;
                     
                     rc = wish_write_packet( state, &itr->second->con, &wp );
                     if( rc != 0 ) {
                        errorf("heartbeat_thread: failed to send, rc = %d\n", rc );
                     }
                     else {
                        // record that we have sent a packet to this peer that we expect an ack for
                        struct wish_heartbeat_packet* whp_dup = (struct wish_heartbeat_packet*)calloc( sizeof(struct wish_heartbeat_packet), 1 );
                        memcpy( whp_dup, &whp, sizeof(whp) );
                        
                        itr->second->pending->push_back( whp_dup );
                        
                        if( itr->second->pending->size() > (unsigned)(_STATUS_MEMORY * 2) ) {
                           // don't keep more than 2x the status memory
                           free( itr->second->pending->front() );
                           itr->second->pending->erase( itr->second->pending->begin() );
                        }
                     }
                  }
                  
                  wish_free_packet( &wp );
               }
            }
         }
      }
      host_heartbeats_unlock();
      usleep( 5000 );
   }
   
   return NULL;
}


// monitor heartbeats for a given connection
int heartbeat_add( struct wish_state* state, struct wish_connection* con, struct wish_packet* wp ) {
   
   if( wp->hdr.type != PACKET_TYPE_HEARTBEAT ) {
      return -EINVAL;
   }
   
   // first, get the name of this host
   char hostname_c[HOST_NAME_MAX+1];
   char portnum_buf[10];
   
   // get the hostname
   int rc = getnameinfo( (struct sockaddr*)&wp->hdr.origin, sizeof(struct sockaddr_storage), hostname_c, HOST_NAME_MAX, portnum_buf, 10, NI_NUMERICSERV );
   if( rc != 0 ) {
      errorf("heartbeat_add: rc = %d, error: '%s', errno = %d\n", rc, gai_strerror( rc ), -errno );
      return -rc;
   }
   
   uint64_t nid = wish_host_nid( hostname_c );
   
   host_heartbeats_wlock();
   
   HostHeartbeats::iterator itr = host_heartbeats.find( nid );
   if( itr == host_heartbeats.end() ) {
      
      struct wish_host_status* host_status = (struct wish_host_status*)calloc( sizeof(struct wish_host_status), 1 );
      
      wish_host_status_init( state, host_status, con );
                             
      // extract the heartbeat information
      struct wish_heartbeat_packet *h = (struct wish_heartbeat_packet*)calloc( sizeof(struct wish_heartbeat_packet), 1 );
      wish_unpack_heartbeat_packet( state, wp, h );
       
      host_status->heartbeats->push_back( h );
      
      host_heartbeats[ nid ] = host_status;
      
      dbprintf("heartbeat_add: will monitor %s (socket %d)\n", hostname_c, host_status->con.soc );
   }
   else {
      // update the connection to this host
      wish_disconnect( state, &itr->second->con );
      itr->second->con = *con;
      dbprintf("heartbeat_add: updated connection for %s (socket %d)\n", hostname_c, itr->second->con.soc );
   }
   
   host_heartbeats_unlock();
   return 0;
}


// process a received heartbeat packet.
// need to write-lock host heartbeats first
// return negative on error, 0 on latency process, 1 if it needs to be acked
int heartbeat_process( struct wish_state* state, struct wish_connection* con, struct wish_packet* wp ) {
   if( wp->hdr.type != PACKET_TYPE_HEARTBEAT )
      return -EINVAL;
   
   // what's the current time (for RTT)?
   struct timeval now;
   memset(&now, 0, sizeof(now) );
   gettimeofday( &now, NULL );
      
   // first, get the name of this host
   char* hostname_c = (char*)calloc( HOST_NAME_MAX + 1, 1 );
   char portnum_buf[10];
   
   // get the hostname
   int rc = getnameinfo( (struct sockaddr*)&wp->hdr.origin, sizeof(struct sockaddr_storage), hostname_c, HOST_NAME_MAX, portnum_buf, 10, NI_NUMERICSERV );
   if( rc != 0 ) {
      errorf("heartbeat_process: rc = %d, error: '%s', errno = %d\n", rc, gai_strerror( rc ), -errno );
      free( hostname_c );
      return -ENETDOWN;
   }
   
   
   // convert to nid
   uint64_t nid = wish_host_nid( hostname_c );
   
   struct wish_host_status* host_status = NULL;
   
   // are we monitoring this host already?
   if( host_heartbeats.find( nid ) != host_heartbeats.end() ) {
      host_status = host_heartbeats[ nid ];
      
      struct wish_heartbeat_packet* ack = (struct wish_heartbeat_packet*)calloc( sizeof(struct wish_heartbeat_packet), 1 );
      wish_unpack_heartbeat_packet( state, wp, ack );
      
      // extract the heartbeat information.
      ack->latency = -1;      // unknown
      
      // is this an acknowledgement of a packet we sent?
      for( vector<struct wish_heartbeat_packet*>::iterator itr = host_status->pending->begin(); itr != host_status->pending->end(); itr++ ) {
         if( *itr == NULL )
            continue;
         
         if( (*itr)->id == ack->id ) {
            // calculate the RTT
            // (now_sec * 1000000L + now_usec - send_sec * 1000000L - send_usec)
            // = (now_sec - send_sec) * 1000000L + (now_usec - send_usec)
            uint64_t d_seconds = now.tv_sec - (*itr)->sendtime.tv_sec;
            uint64_t d_micros = now.tv_usec - (*itr)->sendtime.tv_usec;
            uint64_t rtt = d_seconds * 1000000L + d_micros;
            ack->latency = rtt / 2;
            // remove this pending packet, since it has been acknowledged
            free( *itr );
            host_status->pending->erase( itr );
            break;
         }
      }
      
      if( ack->latency >= 0 ) {
         // success!
         // free the last packet
         if( host_status->heartbeats->size() > (unsigned)_STATUS_MEMORY ) {
            struct wish_heartbeat_packet* old = host_status->heartbeats->front();
            if( old ) {
               free( old );
            }
            host_status->heartbeats->erase( host_status->heartbeats->begin() );
         }
         
         host_status->heartbeats->push_back( ack );
         
         //dbprintf("heartbeat_process: got heartbeat from %s, latency = %ld\n", hostname_c, ack->latency );
      }
      else {
         // not an ack packet
         free( ack );
         rc = 1;
      }
   }
   else {
      // unknown host
      errorf("heartbeat_process: unknown host %s\n", hostname_c );
   }
   
   free( hostname_c );
   
   return rc;
}

// get a connection and NID for a host.
int heartbeat_get_hostname( struct wish_state* state, char const* hostname, struct wish_connection* con, uint64_t* nid ) {
   uint64_t hnid = wish_host_nid( hostname );
   
   int rc = heartbeat_get_nid( state, hnid, con );
   if( rc == 0 )
      *nid = hnid;
   
   return rc;
}

// get a connection to a host, given a nid
int heartbeat_get_nid( struct wish_state* state, uint64_t nid, struct wish_connection* con ) {
   int rc = 0;
   
   wish_state_rlock( state );
   uint64_t my_nid = state->nid;
   int myport = state->conf.portnum;
   wish_state_unlock( state );
   
   if( my_nid == nid ) {
      rc = wish_connect( state, con, "localhost", myport );
   }
   else {
      host_heartbeats_rlock();
      HostHeartbeats::iterator itr = host_heartbeats.find( (long)nid );
      if( itr != host_heartbeats.end() ) {
         rc = wish_connect( state, con, itr->second->hostname, itr->second->portnum );
      }
      else {
         rc = -ENOENT;
      }
      host_heartbeats_unlock();
   }
   
   if( rc == 0 ) {
      // no timeout
      wish_recv_timeout( state, con, 0 ); 
   }
   
   return rc;
}

// nid to hostname
char* heartbeat_nid_to_hostname( struct wish_state* state, uint64_t nid ) {
   char* ret = NULL;
   
   wish_state_rlock( state );
   if( state->nid == nid )
      ret = strdup( state->hostname );
   wish_state_rlock( state );
   
   if( ret )
      return ret;
   
   host_heartbeats_rlock();
   HostHeartbeats::iterator itr = host_heartbeats.find( (long)nid );
   if( itr != host_heartbeats.end() ) {
      ret = strdup( itr->second->hostname );
   }
   host_heartbeats_unlock();
   
   return ret;
}

// nid to portnum
int heartbeat_nid_to_portnum( struct wish_state* state, uint64_t nid ) {
   int ret = -1;
   
   host_heartbeats_rlock();
   HostHeartbeats::iterator itr = host_heartbeats.find( (long)nid );
   if( itr != host_heartbeats.end() ) {
      ret = itr->second->portnum;
   }
   host_heartbeats_unlock();
   
   return ret;
}


// how many nodes do we know about?
uint64_t heartbeat_count_hosts( struct wish_state* state ) {
   host_heartbeats_rlock();
   uint64_t ret = host_heartbeats.size();
   host_heartbeats_unlock();
   return ret;
}


static bool sort_ith_pair( const pair<double, long>& a, const pair<double, long>& b ) {
   return a.first < b.first;
}


static double host_latency( struct wish_host_status* status ) {
   double latency_avg = 0;
   int cnt = 0;
   for( unsigned int i = 0; i < status->heartbeats->size(); i++ ) {
      if( status->heartbeats->at(i)->latency > 0 ) {
         latency_avg += status->heartbeats->at(i)->latency;
         cnt++;
      }
   }
   dbprintf("host_latency: latency of %s is %lf\n", status->hostname, latency_avg / cnt);
   if( cnt == 0 ) {
      return INFINITY;
   }
   return latency_avg / cnt;
}


static double host_ram( struct wish_host_status* status ) {
   double ram_avg = 0;
   int cnt = 0;
   for( unsigned int i = 0; i < status->heartbeats->size(); i++ ) {
      if( status->heartbeats->at(i)->ram_free > 0 ) {
         ram_avg += status->heartbeats->at(i)->ram_free;
         cnt++;
      }
   }
   if( cnt == 0 ) {
      cnt = 1;
   }
   dbprintf("host_ram: RAM of %s is %lf\n", status->hostname, ram_avg / cnt);
   return ram_avg / cnt;
}


static double host_disk( struct wish_host_status* status ) {
   double disk_avg = 0;
   int cnt = 0;
   for( unsigned int i = 0; i < status->heartbeats->size(); i++ ) {
      if( status->heartbeats->at(i)->disk_free > 0 ) {
         disk_avg += status->heartbeats->at(i)->disk_free;
         cnt++;
      }
   }
   if( cnt == 0 ) {
      cnt = 1;
   }
   dbprintf("host_disk: disk of %s is %lf\n", status->hostname, disk_avg / cnt );
   return disk_avg / cnt;
}


static double host_cpu( struct wish_host_status* status ) {
   double cpu_avg = 0;
   int cnt = 0;
   for( unsigned int i = 0; i < status->heartbeats->size(); i++ ) {
      if( status->heartbeats->at(i)->loads[0] > 0 ) {
         cpu_avg += status->heartbeats->at(i)->loads[0];
         cnt++;
      }
   }
   if( cnt == 0 )
      cnt = 1;
   
   dbprintf("host_cpu: cpu of %s is %lf\n", status->hostname, cpu_avg / cnt );
   return cpu_avg / cnt;
}


static uint64_t host_best( struct wish_state* state, unsigned int best, double (*attr_calc)(struct wish_host_status*), double local, bool reverse ) {
   
   // which host has the ith best attr?
   host_heartbeats_rlock();
   vector< pair<double, long> > buf;      // list of <nid, avg latency>
   for( HostHeartbeats::iterator itr = host_heartbeats.begin(); itr != host_heartbeats.end(); itr++ ) {
      double val = (*attr_calc)( itr->second );
      buf.push_back( pair<double, long>( val, wish_host_nid( itr->second->hostname ) ) );
   }
   
   wish_state_rlock( state );
   buf.push_back( pair<double, long>( local, state->nid ) );
   wish_state_unlock( state );
   
   sort( buf.begin(), buf.end(), sort_ith_pair );
   host_heartbeats_unlock();
   
   unsigned int ind = 0;
   if( reverse )
      ind = best;
   else
      ind = buf.size()-1-best;
   
   return buf[ind].second;
}

// which host has the ith best latency?
uint64_t heartbeat_best_latency( struct wish_state* state, unsigned int best ) {
   return host_best( state, best, host_latency, 0, true );
}

// which host has the ith best cpu?
uint64_t heartbeat_best_cpu( struct wish_state* state, unsigned int best ) {
   struct sysinfo sys;
   sysinfo( &sys );
   
   return host_best( state, best, host_cpu, sys.loads[0], true );
}

// which host has the ith most free ram?
uint64_t heartbeat_best_ram( struct wish_state* state, unsigned int best ) {
   struct sysinfo sys;
   sysinfo( &sys );
   
   return host_best( state, best, host_ram, sys.freeram + sys.bufferram, false );
}

// which host has the ith most free disk?
uint64_t heartbeat_best_disk( struct wish_state* state, unsigned int best ) {
   wish_state_rlock( state );
   struct statfs fs;
   int rc = statfs( state->conf.files_root, &fs );
   wish_state_unlock( state );
   
   double val;
   if( rc < 0 )
      val = 0;
   else
      val = fs.f_bavail * fs.f_bsize;
   
   return host_best( state, best, host_disk, val, false );
}

uint64_t heartbeat_index( struct wish_state* state, unsigned int best ) {
   
   wish_state_rlock( state );
   uint64_t nid = 0;
   if( best == 0 )
      nid = state->nid;
   else
      nid = wish_host_nid( state->conf.initial_peers[best-1]->hostname );
   wish_state_unlock( state );
   return nid;
}
