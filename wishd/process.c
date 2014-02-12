#include "process.h"

typedef map<uint64_t, struct wish_process*> ProcessTable;
typedef map<uint64_t, struct wish_spawn*> SpawnTable;

// table of processes we're executing
static ProcessTable procs;
static pthread_rwlock_t procs_lock;

// table of processes we've spawned
static SpawnTable spawned;
static pthread_rwlock_t spawned_lock;

// thread that writes back stdout and stderr of locally-running processes to the originator
static pthread_t process_writeback_thread;

// thread that processes feedback from remotely-executing processes
static pthread_t process_spawned_eventloop_thread;

// thread that processes feedback to locally executing procsses
static pthread_t process_proc_eventloop_thread;

// writeback thread function
void* process_writeback_func( void* arg );

// spawned event loop function
void* process_spawned_eventloop_func( void* arg );

// process event loop function
void* process_proc_eventloop_func( void* arg );

// NIDs of possible localhost names
static vector<uint64_t> localhost_nids;

static int wish_finish_process( struct wish_state* state, struct wish_process** proc );
static int wish_spawned_destroy( struct wish_state* state, struct wish_spawn* spawned );

// initialize processes
int process_init( struct wish_state* state ) {
   
   pthread_rwlock_init( &procs_lock, NULL );
   pthread_rwlock_init( &spawned_lock, NULL );
   
   int rc;
   
   rc = pthread_create( &process_writeback_thread, NULL, process_writeback_func, state );
   if( rc != 0 ) {
      pthread_rwlock_destroy( &procs_lock );
      pthread_rwlock_destroy( &spawned_lock );
      return rc;
   }
   
   rc = pthread_create( &process_spawned_eventloop_thread, NULL, process_spawned_eventloop_func, state );
   if( rc != 0 ) {
      pthread_kill( process_writeback_thread, SIGKILL );
      pthread_rwlock_destroy( &procs_lock );
      pthread_rwlock_destroy( &spawned_lock );
      return rc;
   }
   
   
   rc = pthread_create( &process_proc_eventloop_thread, NULL, process_proc_eventloop_func, state );
   if( rc != 0 ) {
      pthread_kill( process_writeback_thread, SIGKILL );
      pthread_kill( process_spawned_eventloop_thread, SIGKILL );
      pthread_rwlock_destroy( &procs_lock );
      pthread_rwlock_destroy( &spawned_lock );
      return rc;
   }
   
   
   localhost_nids.push_back( wish_host_nid( "127.0.0.1" ) );
   localhost_nids.push_back( wish_host_nid( "127.0.1.1" ) );
   localhost_nids.push_back( wish_host_nid( "localhost" ) );
   localhost_nids.push_back( wish_host_nid( "::1" ) );
   localhost_nids.push_back( wish_host_nid( "localhost.localdomain" ) );
   
   return rc;
}

// shut down processes
int process_shutdown( struct wish_state* state ) {
   
   pthread_kill( process_writeback_thread, SIGKILL );
   pthread_kill( process_spawned_eventloop_thread, SIGKILL );
   pthread_kill( process_proc_eventloop_thread, SIGKILL );
   
   pthread_join( process_writeback_thread, NULL );
   pthread_join( process_spawned_eventloop_thread, NULL );
   pthread_join( process_proc_eventloop_thread, NULL );
   
   for( ProcessTable::iterator itr = procs.begin(); itr != procs.end(); itr++ ) {
      wish_finish_process( state, &itr->second );
   }
   for( SpawnTable::iterator itr = spawned.begin(); itr != spawned.end(); itr++ ) {
      wish_spawned_destroy( state, itr->second );
      free( itr->second );
      itr->second = NULL;
   }
   
   pthread_rwlock_destroy( &procs_lock );
   pthread_rwlock_destroy( &spawned_lock );
   
   return 0;
}

// read-lock the process table
int procs_rlock(void) {
   return pthread_rwlock_rdlock( &procs_lock );
}

// write-lock the process table
int procs_wlock(void) {
   return pthread_rwlock_wrlock( &procs_lock );
}

// unlock the process table
int procs_unlock(void) {
   return pthread_rwlock_unlock( &procs_lock );
}

// read-lock the process table
int spawned_rlock(void) {
   return pthread_rwlock_rdlock( &spawned_lock );
}

// write-lock the process table
int spawned_wlock(void) {
   return pthread_rwlock_wrlock( &spawned_lock );
}

// unlock the process table
int spawned_unlock(void) {
   return pthread_rwlock_unlock( &spawned_lock );
}


// make a process entry
static int wish_process_init( struct wish_state* state, struct wish_process* proc, pid_t pid, uint64_t gpid, struct wish_connection* con, int stdout_fd, int stderr_fd, time_t timeout ) {
   proc->pid = pid;
   proc->gpid = gpid;
   proc->stdout_fd = stdout_fd;
   proc->stderr_fd = stderr_fd;
   proc->con = con;
   
   if( timeout >= 0 ) {
      proc->expire = time(NULL) + timeout;
   }
   return 0;
}

// make a spawned process entry
static int wish_spawned_init( struct wish_state* state, struct wish_spawn* spawned, struct wish_job_packet* job ) {
   memset( spawned, 0, sizeof(struct wish_spawn) );
   spawned->gpid = job->gpid;
   spawned->timeout = job->timeout;
   spawned->start_time = -1;
   spawned->status = PROCESS_STATUS_INIT;
   spawned->flags = job->flags;
   return 0;
}


// destroy a process entry
static int wish_process_destroy( struct wish_state* state, struct wish_process* proc ) {
   if( proc->con ) {
      wish_disconnect( state, proc->con );
      free( proc->con );
      proc->con = NULL;
   }
   if( proc->stdout_fd >= 0 )
      close( proc->stdout_fd );
   if( proc->stderr_fd >= 0 )
      close( proc->stderr_fd );
   if( proc->stdout_path )
      free( proc->stdout_path );
   if( proc->stderr_path )
      free( proc->stderr_path );
   
   memset( proc, 0, sizeof(struct wish_process) );
   return 0;
}


// destroy a spawned entry
static int wish_spawned_destroy( struct wish_state* state, struct wish_spawn* spawned ) {
   dbprintf("wish_spawned_destroy: destroy %lu\n", spawned->gpid );
   if( spawned->con ) {
      wish_disconnect( state, spawned->con );
      free( spawned->con );
      spawned->con = NULL;
   }
   if( spawned->client ) {
      wish_disconnect( state, spawned->client );
      free( spawned->client );
      spawned->client = NULL;
   }
   if( spawned->join ) {
      wish_disconnect( state, spawned->join );
      free( spawned->join );
   }
   if( spawned->stdout ) {
      fclose( spawned->stdout );
      spawned->stdout = NULL;
   }
   if( spawned->stderr ) {
      fclose( spawned->stderr );
      spawned->stderr = NULL;
   }
   return 0;
}

// read data into a string packet
static int wish_process_read_output( struct wish_state* state, char which, int fd, struct wish_strings_packet* wssp ) {
   
   // get the pending data
   char buf[PROCESS_READ_SIZE+1];
   memset(buf, 0, PROCESS_READ_SIZE+1);
   
   ssize_t count = read( fd, buf, PROCESS_READ_SIZE );
   if( count < 0 ) {
      return -errno;
   }
   else if( count > 0 ) {
      struct wish_string_packet pkt;
      wish_init_string_packet( state, &pkt, which, buf );
      
      dbprintf("wish_process_read_input: read %ld bytes\n", count);
      wish_add_string_packet( state, wssp, &pkt );
      wish_free_string_packet( &pkt );
      return 0;
   }
   else {
      return -ENODATA;
   }
}


// kill a process
static int wish_kill_process( struct wish_state* state, struct wish_process* proc ) {
   return kill( proc->pid, SIGKILL );
}


// finish off a running process
static int wish_finish_process( struct wish_state* state, struct wish_process** proc ) {
   unlink( (*proc)->stdout_path );
   unlink( (*proc)->stderr_path );
   
   wish_process_destroy( state, *proc );
   
   free( *proc );
   *proc = NULL;
   return 0;
}

// write back a process packet to a client
int wish_process_reply( struct wish_state* state, struct wish_connection* con, int type, uint64_t gpid, int data ) {
   struct wish_process_packet ppkt;
   struct wish_packet pkt;
      
   // send back an error code
   wish_init_process_packet( state, &ppkt, type, gpid, 0, data );
   wish_pack_process_packet( state, &pkt, &ppkt );
   
   int rc = wish_write_packet( state, con, &pkt );
   wish_free_packet( &pkt );
   
   if( rc != 0 ) {
      errorf("wish_process_reply: wish_write_packet rc = %d\n", rc );
   }
   
   return rc;
}


// constantly write back stdout and stderr to an origin.
// clear out any timed-out processes
void* process_writeback_func( void* arg ) {
   
   struct wish_state* state = (struct wish_state*)arg;
   
   fd_set rfds;
   struct timeval tv;
   
   while( 1 ) {
      tv.tv_sec = 0;
      tv.tv_usec = 1000;
   
      FD_ZERO( &rfds );
      int max_fd = -1;
      
      vector<uint64_t> dead;
      
      procs_wlock();
      for( ProcessTable::iterator itr = procs.begin(); itr != procs.end(); itr++ ) {
         if( itr->second == NULL ) {
            dead.push_back( itr->first );
            continue;
         }
         
         if( itr->second->stdout_fd >= 0 ) 
            FD_SET(itr->second->stdout_fd, &rfds);
         
         if( itr->second->stderr_fd >= 0 )
            FD_SET(itr->second->stderr_fd, &rfds);
         
         max_fd = MAX( MAX(itr->second->stdout_fd, itr->second->stderr_fd), max_fd );
      }
      
      if( max_fd > 0 ) {
         // do the select
         // NOTE: some fds could disappear if we unlocked procs, so do that later
         int fds_ready = select( max_fd + 1, &rfds, NULL, NULL, &tv );
         if( fds_ready > 0 ) {
            // some fds are ready to be read from.
            // build up a wish_strings_packet containing the information
            
            for( ProcessTable::iterator itr = procs.begin(); itr != procs.end(); itr++ ) {
               if( itr->second == NULL ) {
                  continue;
               }
               
               bool data = false;
               
               struct wish_strings_packet wssp;
               wish_init_strings_packet( state, &wssp, fds_ready );
               
               if( itr->second->stdout_fd > 0 ) {
                  if( FD_ISSET( itr->second->stdout_fd, &rfds ) ) {
                     int rc = wish_process_read_output( state, STRING_STDOUT, itr->second->stdout_fd, &wssp );
                     if( rc != 0 ) {
                        if( rc != -ENODATA ) {
                           // not an EOF error
                           errorf("process_writeback_func: could not read from %s, rc = %d\n", itr->second->stdout_path, rc );
                        }
                     }
                     else {
                        data = true;
                     }
                  }
               }
               
               if( itr->second->stderr_fd > 0 ) {
                  if( FD_ISSET( itr->second->stderr_fd, &rfds ) ) {
                     int rc = wish_process_read_output( state, STRING_STDERR, itr->second->stderr_fd, &wssp );
                     if( rc != 0 ) {
                        if( rc != -ENODATA ) {
                           // not an EOF error
                           errorf("process_writeback_func: could not read from %s, rc = %d\n", itr->second->stderr_path, rc );
                        }
                     }
                     else {
                        data = true;
                     }
                  }
               }
               if( !data ) {
                  wish_free_strings_packet( &wssp );
                  
                  // nothing to read.  Is this process dead?
                  if( itr->second->finished ) {
                     wish_finish_process( state, &itr->second );
                     dead.push_back( itr->first );
                  }
                  continue;
               }
               
               // send off the data!
               struct wish_packet pkt;
               wish_pack_strings_packet( state, &pkt, &wssp );
               
               int rc = wish_write_packet( state, itr->second->con, &pkt );
               wish_free_packet( &pkt );
               wish_free_strings_packet( &wssp );
               
               if( rc != 0 ) {
                  // problem sending!
                  errorf("process_writeback_func: send rc = %d\n", rc );
                  if( rc == -EBADF ) {
                     // something broke
                     wish_finish_process( state, &itr->second );
                     dead.push_back( itr->first );
                  }
               }
            }
         }
         else if( fds_ready < 0 ) {
            // an error occurred
            errorf("process_writeback_func: select errno = %d\n", -errno);
            
            // find the offending process, and just erase it
            for( ProcessTable::iterator itr = procs.begin(); itr != procs.end(); itr++ ) {
               int rc = fcntl( itr->second->stdout_fd, F_GETFL );
               if( rc == -1 && errno == -EBADF ) {
                  // it's bad
                  wish_finish_process( state, &itr->second );
                  dead.push_back( itr->first );
               }
               else {
                  rc = fcntl( itr->second->stderr_fd, F_GETFL );
                  if( rc == -1 && errno == -EBADF ) {
                     // it's bad
                     wish_finish_process( state, &itr->second );
                     dead.push_back( itr->first );
                  }
               }
            }
         }
         
         // clear out erased entries
         for(vector<uint64_t>::size_type i = 0; i < dead.size(); i++ ) {
            procs.erase( dead[i] );
         }
      }
      
      procs_unlock();
      usleep(10000);
   }
   
   return NULL;
}


// eventloop--read and process packets from remotely-running processes
void* process_spawned_eventloop_func( void* arg ) {
   struct wish_state* state = (struct wish_state*)arg;
   
   fd_set rfds;
   struct timeval tv;
   
   while( 1 ) {
      tv.tv_sec = 0;
      tv.tv_usec = 100;
   
      FD_ZERO( &rfds );
      int max_fd = -1;
      
      vector<uint64_t> dead;
      
      time_t current_time = time(NULL);
      
      // see if any of our spawned processes have input for us to process
      spawned_wlock();
      for( SpawnTable::iterator itr = spawned.begin(); itr != spawned.end(); itr++ ) {
         if( itr->second == NULL ) {
            dead.push_back( itr->first );
            continue;
         }
         if( 0 < itr->second->timeout && itr->second->start_time + itr->second->timeout > current_time ) {
            // process has timed out
            wish_spawned_destroy( state, itr->second );
            free( itr->second );
            itr->second = NULL;
            dead.push_back( itr->second->gpid );
            continue;
         }
         
         // otherwise, add to our fdset
         if( itr->second->con->soc >= 0 ) {
            FD_SET( itr->second->con->soc, &rfds );
            max_fd = MAX( itr->second->con->soc, max_fd );
         }
      }
      
      if( max_fd > 0 ) {
         
         int fds_ready = select( max_fd + 1, &rfds, NULL, NULL, &tv );
         if( fds_ready > 0 ) {
            // we have work to do!
            for( SpawnTable::iterator itr = spawned.begin(); itr != spawned.end(); itr++ ) {
               if( itr->second == NULL ) {
                  continue;
               }
               if( itr->second->con->soc < 0 )
                  continue;
               if( !FD_ISSET( itr->second->con->soc, &rfds ) )
                  continue;
               
               struct wish_packet pkt;
               pkt.hdr.type = -1;
               
               int rc = wish_read_packet_noblock( state, itr->second->con, &pkt );
               if( rc != 0 ) {
                  if( rc != -EAGAIN && rc != -EHOSTDOWN ) {
                     errorf("process_spawned_eventloop_func: wish_read_packet rc = %d\n", rc );
                  }
               }
               else {
                  if( pkt.hdr.type == PACKET_TYPE_PROCESS ) {
                     // process control packet
                     struct wish_process_packet wpp;
                     wish_unpack_process_packet( state, &pkt, &wpp );
                     rc = process_update( state, &wpp );
                     if( rc != 0 ) {
                        if( rc == PROCESS_UPDATE_DESTROYED ) {
                           // the process died
                           dead.push_back( itr->first );
                        }
                        else {
                           errorf("process_spawned_eventloop_func: process_update rc = %d\n", rc );
                        }
                     }
                  }
                  else if( pkt.hdr.type == PACKET_TYPE_STRINGS ) {
                     // stdout/stderr data
                     
                     vector<struct wish_string_packet*> unwritten;
                     
                     // redirect the appropriate strings
                     struct wish_strings_packet wssp;
                     wish_unpack_strings_packet( state, &pkt, &wssp );
                     
                     for( int i = 0; i < wssp.count; i++ ) {
                        
                        if( wssp.packets[i].which == STRING_STDOUT && itr->second->stdout != NULL ) {
                           fwrite( wssp.packets[i].str, 1, strlen(wssp.packets[i].str), itr->second->stdout );
                           wish_free_string_packet( &wssp.packets[i] );
                        }
                        else if( wssp.packets[i].which == STRING_STDERR && itr->second->stderr != NULL ) {
                           fwrite( wssp.packets[i].str, 1, strlen(wssp.packets[i].str), itr->second->stderr );
                           wish_free_string_packet( &wssp.packets[i] );
                        }
                        else {
                           unwritten.push_back( &wssp.packets[i] );
                        }
                     }
                     
                     struct wish_strings_packet to_client;
                     wish_init_strings_packet( state, &to_client, unwritten.size() );
                     for( unsigned int i = 0; i < unwritten.size(); i++ ) {
                        wish_add_string_packet( state, &to_client, unwritten[i] );
                     }
                     
                     struct wish_packet to_client_packet;
                     wish_pack_strings_packet( state, &to_client_packet, &to_client );
                     
                     // forward to the client
                     rc = 0;
                     
                     wish_state_rlock( state );
                     vector<struct wish_connection*>* client_cons = state->client_cons;
                     
                     if( client_cons->size() > 0 ) {
                        for( uint64_t i = 0; i < client_cons->size(); i++ ) {
                           rc = wish_write_packet( state, client_cons->at(i), &to_client_packet );
                           if( rc != 0 ) {
                              errorf("process_spawned_eventloop_func: wish_write_packet to client on %d rc = %d\n", client_cons->at(i)->soc, rc );
                              wish_disconnect( state, client_cons->at(i) );
                              free( client_cons->at(i) );
                              (*client_cons)[i] = NULL;
                           }
                        }
                     }
                     wish_state_unlock( state );
                     
                     wish_free_strings_packet( &wssp );
                     wish_free_strings_packet( &to_client );
                     wish_free_packet( &to_client_packet );
                     
                  }
                  
                  else {
                     // unknown packet type
                     errorf("process_spawned_eventloop_func: unknown packet type %d\n", pkt.hdr.type );
                     
                     // drain the socket--probably have some garbage in it
                     wish_clear_connection( state, itr->second->con );
                  }
                  
                  wish_free_packet( &pkt );
               }
            }
         }
         else if( fds_ready < 0 ) {
            errorf("process_spawned_eventloop_func: select errno = %d\n", -errno );
            
            // find the offending socket
            for( SpawnTable::iterator itr = spawned.begin(); itr != spawned.end(); itr++ ) {
               if( itr->second->con->soc < 0 ) {
                  // connection dead
                  errorf("process_spanwed_eventloop_func: lost connection to %lu\n", itr->second->gpid );
                  wish_spawned_destroy( state, itr->second );
                  free( itr->second );
                  dead.push_back( itr->first );
                  continue;
               }
               
               // see if we can do a socket op on it....
               struct timeval tv;
               socklen_t sz = sizeof(tv);
               int rc = getsockopt( itr->second->con->soc, SOL_SOCKET, SO_RCVTIMEO, &tv, &sz );
               if( rc == -1 && errno == -EBADF ) {
                  // it's bad
                  errorf("process_spanwed_eventloop_func: lost connection to %lu\n", itr->second->gpid );
                  wish_spawned_destroy( state, itr->second );
                  free( itr->second );
                  dead.push_back( itr->first );
               }
            }
            errno = 0;
         }
         
         // clear out dead processes
         for( vector<uint64_t>::size_type i = 0; i < dead.size(); i++ ) {
            spawned.erase( dead[i] );
         }
      }
      spawned_unlock();
      usleep( 10000 );
   }
   
   return NULL;
}


// eventloop--read and process packets from locally-running processes
void* process_proc_eventloop_func( void* arg ) {
   struct wish_state* state = (struct wish_state*)arg;
   
   fd_set rfds;
   struct timeval tv;
   
   while( 1 ) {
      tv.tv_sec = 0;
      tv.tv_usec = 100;
   
      FD_ZERO( &rfds );
      int max_fd = -1;
      
      vector<uint64_t> dead;
      
      time_t current_time = time(NULL);
      
      // see if any of our running processes have input for us to process
      procs_wlock();
      for( ProcessTable::iterator itr = procs.begin(); itr != procs.end(); itr++ ) {
         if( itr->second == NULL ) {
            dead.push_back( itr->first );
            continue;
         }
         
         if( 0 < itr->second->expire && itr->second->expire < current_time ) {
            
            // kill this process--it's timed out
            errorf("process_proc_eventloop_func: process %lu timed out\n", itr->second->gpid );
            wish_kill_process( state, itr->second );
            wish_process_reply( state, itr->second->con, PROCESS_TYPE_TIMEOUT, itr->second->gpid, 0 );
            wish_finish_process( state, &itr->second );
            dead.push_back( itr->first );
            continue;
         }
         
         // otherwise, add to our fdset
         if( itr->second->con->soc >= 0 ) {
            FD_SET( itr->second->con->soc, &rfds );
            max_fd = MAX( itr->second->con->soc, max_fd );
         }
      }
      
      if( max_fd > 0 ) {
         int fds_ready = select( max_fd + 1, &rfds, NULL, NULL, &tv );
         if( fds_ready > 0 ) {
            // we have work to do!
            for( ProcessTable::iterator itr = procs.begin(); itr != procs.end(); itr++ ) {
               if( itr->second == NULL ) {
                  continue;
               }
               if( itr->second->con->soc < 0 )
                  continue;
               
               if( !FD_ISSET( itr->second->con->soc, &rfds ) )
                  continue;
               
               struct wish_packet pkt;
               pkt.hdr.type = -1;
               
               // NOTE: no need for non-blocking I/O--processes are children of us
               int rc = wish_read_packet_noblock( state, itr->second->con, &pkt );
               
               if( rc != 0 ) {
                  if( rc != -EAGAIN ) {
                     errorf("process_proc_eventloop_func: wish_read_packet rc = %d\n", rc );
                     wish_disconnect( state, itr->second->con );
                     dead.push_back( itr->first );
                  }
               }
               else {
                  if( pkt.hdr.type == PACKET_TYPE_PROCESS ) {
                     // process control packet
                     struct wish_process_packet wpp;
                     wish_unpack_process_packet( state, &pkt, &wpp );
                     
                     switch( wpp.type ) {
                        
                        case PROCESS_TYPE_PSIG: {
                           // got a signal
                           rc = process_recv_signal( state, wpp.gpid, wpp.signal );
                           if( rc != 0 ) {
                              errorf("process_proc_eventloop_func: failed to signal %lu\n", wpp.gpid);
                           }
                           break;
                        }
                        case PROCESS_TYPE_PSIGALL: {
                           // got sigall
                           rc = process_recv_signal_all( state, wpp.signal );
                           if( rc != 0 ) {
                              errorf("process_proc_eventloop_func: failed to send signal %d to %d process(es)\n", wpp.signal, rc );
                           }
                           break;
                        }
                        default: {
                           // unknown
                           errorf("process_proc_eventloop_func: unknown process packet %d\n", wpp.type );
                        }
                     }
                  }
                  else {
                     // unknown packet type
                     errorf("process_proc_eventloop_func: unknown packet type %d\n", pkt.hdr.type );
                     
                     // drain the socket--probably have some garbage in it
                     wish_clear_connection( state, itr->second->con );
                  }
                  
                  wish_free_packet( &pkt );
               }
            }
         }
         else if( fds_ready < 0 ) {
            if( errno != EBADF )
               errorf("process_proc_eventloop_func: select errno = %d\n", -errno );
            
            // find the offending socket
            for( ProcessTable::iterator itr = procs.begin(); itr != procs.end(); itr++ ) {
               struct timeval tv;
               socklen_t sz = sizeof(tv);
               int rc = getsockopt( itr->second->con->soc, SOL_SOCKET, SO_RCVTIMEO, &tv, &sz );
               if( rc == -1 && errno == -EBADF ) {
                  // it's bad--disconnect
                  wish_disconnect( state, itr->second->con );
               }
            }
            errno = 0;
         }
         
         // clear out dead processes
         for( vector<uint64_t>::size_type i = 0; i < dead.size(); i++ ) {
            procs.erase( dead[i] );
         }
      }
      procs_unlock();
      usleep( 10000 );
   }
   
   return NULL;
}


// read n bytes, blocking
static ssize_t read_bytes( int fd, void* dest, size_t count ) {
   ssize_t ret = 0;
   ssize_t tmp = 0;
   while( ret < (signed)count ) {
      tmp = read( fd, (uint8_t*)dest + ret, count - ret );
      if( tmp <= 0 ) {
         if( tmp == 0 ) {
            return 0;
         }
         else {
            return -errno;
         }
      }
      else {
         ret += tmp;
      }
   }
   return ret;
}



// run a shell command, and write the stdout and stderr to disk for recovery.
static int process_run( struct wish_state* state,
                        struct wish_connection* con,
                        struct wish_job_packet* job,
                        struct wish_process* proc,
                        int child_stdin,
                        int child_stdout,
                        int child_stderr,
                        char* stdout_path,
                        char* stderr_path) {
   
   int rc = 0;
   
   // compute the shell command
   wish_state_rlock( state );
   char** shell_argv = (char**)calloc( sizeof(char*) * (state->conf.shell_argc + 3), 1 );
   
   shell_argv[0] = strdup( state->conf.shell );
   for( int i = 0; i < state->conf.shell_argc; i++ ) {
      shell_argv[i+1] = strdup( state->conf.shell_argv[i] );
   }
   
   wish_state_unlock( state );
   
   shell_argv[ state->conf.shell_argc + 1 ] = strdup( job->cmd_text );
   
   // make a pipe between the daemon process and the shell wrapper process
   int wrapper_fds[2];
   pipe( wrapper_fds );
   
   // need to fork a wrapper process first to safely close stdin, stdout, stderr.
   pid_t pid1 = fork();
   if( pid1 == 0 ) {
      // wrapper process (shell process parent)
      // save old stdin, stdout, stderr
      int old_stdin, old_stdout, old_stderr;

      old_stdin = dup( STDIN_FILENO );
      old_stdout = dup( STDOUT_FILENO );
      old_stderr = dup( STDERR_FILENO );

      close( STDIN_FILENO );
      close( STDOUT_FILENO );
      close( STDERR_FILENO );

      // set up stdin, stdout, stderr for child process
      dup2( child_stdin, STDIN_FILENO );
      dup2( child_stdout, STDOUT_FILENO );
      dup2( child_stderr, STDERR_FILENO );
      
      // fork again to run the shell command
      pid_t shell_pid = fork();
      if( shell_pid == 0 ) {
         // set up environment
         // extract hostname and portnum from the origin daemon
         char hostname[HOST_NAME_MAX+1];
         char portnum_txt[10];
         char gpid_txt[20];
         char portnum_buf[10];
         
         int rc = getnameinfo( (struct sockaddr*)&job->visited[0], sizeof(struct sockaddr_storage), hostname, HOST_NAME_MAX, portnum_txt, 10, NI_NUMERICSERV );
         if( rc != 0 ) {
            //errorf("Will not execute--could not set up environment (%s)\n", gai_strerror(rc) );
            exit(-1);
         }
         
         sprintf(gpid_txt, "%lu", job->gpid );
         sprintf(portnum_buf, "%d", job->origin_http_portnum);
         
         setenv( WISH_ORIGIN_ENV, hostname, 1 );
         setenv( WISH_PORTNUM_ENV, portnum_txt, 1 );
         setenv( WISH_GPID_ENV, gpid_txt, 1 );
         setenv( WISH_HTTP_PORTNUM_ENV, portnum_buf, 1 );
         
         // run the shell command.
         execv( shell_argv[0], shell_argv );
      }
      else if( shell_pid > 0 ) {
         // wrapper process--collect the child's rc and give it to the daemon
         // restore stdin, stdout, stderr
         close( STDIN_FILENO );
         close( STDOUT_FILENO );
         close( STDERR_FILENO );
         dup2( old_stdin, STDIN_FILENO );
         dup2( old_stdout, STDOUT_FILENO );
         dup2( old_stderr, STDERR_FILENO );
         close( wrapper_fds[0] );
         
         // write back the shell process's pid
         write( wrapper_fds[1], &shell_pid, sizeof(shell_pid) );
         
         // join with shell process to get its exit and signal
         int shell_rc = 0;
         errno = 0;
         rc = waitpid( shell_pid, &shell_rc, 0 );
         
         // send back the shell rc
         if( rc > 0 ) {
            write( wrapper_fds[1], &shell_rc, sizeof(shell_rc) );
         }
         else {
            errorf("wrapper: failed to waitpid; errno = %d\n", -errno);
            rc = -errno;
         }
            
         exit(rc);
      }
      else {
         exit(-errno);
      }
   }
   else if( pid1 > 0 ) {
      // daemon
      
      // read the shell process id
      pid_t shell_pid = -1;
      rc = read_bytes( wrapper_fds[0], &shell_pid, sizeof(shell_pid) );
      if( rc == sizeof(shell_pid) ) {
         dbprintf("process_run: PID = %d\n", shell_pid);
         
         // record this process's information
         int proc_stdout = open( stdout_path, O_RDONLY );
         int proc_stderr = open( stderr_path, O_RDONLY );
         wish_process_init( state, proc, shell_pid, job->gpid, con, proc_stdout, proc_stderr, job->timeout );
         
         proc->stdout_path = stdout_path;
         proc->stderr_path = stderr_path;
         
         procs_wlock();
         procs[ proc->gpid ] = proc;
         procs_unlock();
         
         // tell the remote caller that this process started
         rc = wish_process_reply( state, con, PROCESS_TYPE_STARTED, job->gpid, 0 );
         
         if( rc != 0 ) {
            // failed to send--write the error back to the parent
            errorf("process_run: wish_write_packet (started) rc = %d\n", rc );
         }
         
         // get the wrapper's rc and shell rc information
         int wrapper_rc = 0;
         int shell_rc = 0;
         
         rc = waitpid( pid1, &wrapper_rc, 0 );
         if( rc > 0 && wrapper_rc > 0 ) {
            // wrapper returned successfully
            rc = read_bytes( wrapper_fds[0], &shell_rc, sizeof(shell_rc) );
            if( rc == sizeof(shell_rc) ) {
               dbprintf("process_run: exit code %d for %lu\n", WEXITSTATUS(shell_rc), job->gpid );
               rc = wish_process_reply( state, con, PROCESS_TYPE_EXIT, job->gpid, shell_rc );
               
               if( rc != 0 ) {
                  errorf("process_run: wish_write_packet (exited) rc = %d on %d\n", rc, con->soc );
               }
            }
            else {
               errorf("process_run: read_bytes from wrapper pipe rc = %d\n", rc );
            }
         }
         else {
            // wrapper failure
            errorf("process_run: waitpid rc = %d, wrapper process rc = %d, for job %lu\n", rc, wrapper_rc, job->gpid );
            rc = wrapper_rc;
            
            wish_process_reply( state, con, PROCESS_TYPE_ERROR, job->gpid, 0 );
         }
      
         // mark the process as terminated.
         // the writeback function will clear it.
         procs_wlock();
         if( procs.find( proc->gpid ) != procs.end() ) {
            procs[ proc->gpid ]->finished = true;
         }
         procs_unlock();
      }
      else {
         // broken pipe, somehow
         errorf("process_run: could not read from wrapper shell process pipe, rc = %d\n", rc );
         free( proc );
      }
      
      close( child_stdout );
      close( child_stderr );
   }
   else {
      // fork failure
      rc = -errno;
   }
   
   close( wrapper_fds[0] );
   close( wrapper_fds[1] );
   
   // free memory
   for( int i = 0; shell_argv[i] != NULL; i++ ) {
      free( shell_argv[i] );
   }
   free( shell_argv );
   
   return rc;
}


// convert a job URL and a nid to an HTTP url
static char* process_url_reformat( struct wish_state* state, char* url, uint64_t nid, char const* src_proto, char const* dest_proto ) {
   char* path = NULL;
   if( src_proto ) {
      path = strstr( url + strlen(src_proto) + strlen("://"), "/" );
   }
   else {
      path = strstr( url, "/" );
   }
   
   // host id is a number--it's a NID.
   char* hostname = heartbeat_nid_to_hostname( state, nid );
   if( hostname == NULL ) {
      // not found
      return NULL;
   }
   
   int portnum = heartbeat_nid_to_portnum( state, nid );
   
   char* full_url = (char*)calloc( 10 + strlen(hostname) + 5 + strlen(url), 1 );
   
   sprintf(full_url, "%s://%s:%d/%s", dest_proto, hostname, portnum, path );
   
   free( hostname );
   return full_url;
}


// get a file and put it into place
static int process_get_file( struct wish_state* state, struct wish_connection* con, struct wish_job_packet* job, char* url, int fd ) {
   wish_state_rlock( state );
   bool https = state->conf.use_https;
   wish_state_unlock( state );
   
   // get stdin and put it into place
   if( strlen(url) > 0 ) {
      
      dbprintf("process_get_file: url = %s\n", url );
      char* full_url = NULL;
      
      // no protocol?  does this look like $NUMBER/path, or $HOSTNAME/path?
      if( strstr( url, "://" ) == NULL ) {
         char* tmp = NULL;
         char* stdin_url_dup = strdup( url );
         
         char* host_id = strtok_r( stdin_url_dup, "/", &tmp );
         
         char* endptr = NULL;
         uint64_t nid = (uint64_t)strtol( host_id, &endptr, 10 );
         if( endptr == host_id ) {
            full_url = process_url_reformat( state, url, nid, NULL, (https ? "https" : "http") );
         }
         else {
            full_url = process_url_reformat( state, url, nid, NULL, (https ? "https" : "http") );
         }
         free( stdin_url_dup );
      }
      
      // is this http:// or https:// or ftp://?
      if( strstr(url, "http://") == url || strstr(url,"https://") == url || strstr(url,"ftp://") == url) {
         full_url = strdup( url );
      }
      
      if( full_url ) {
         struct wish_HTTP_info resp_stdin;
         memset( &resp_stdin, 0, sizeof(resp_stdin) );
         
         int rc = wish_HTTP_download_file( state, &resp_stdin, full_url, NULL, NULL, fd );
         
         if( rc != 0 || resp_stdin.status != 200 ) {
            // failed to get stdin
            errorf("process_get_file: could not download from %s (original url = %s), HTTP status = %d, rc = %d\n", full_url, url, resp_stdin.status, rc );
      
            wish_process_reply( state, con, PROCESS_TYPE_FAILURE, job->gpid, 0 );
            
            return -abs(rc);
         }
         
         free( full_url );
         wish_free_HTTP_info( &resp_stdin );
      }
      else {
         // no protocol determined
         errorf("process_get_file: could not determine HTTP URL for %s\n", url );
         return -EINVAL;
      }
   }
   
   return 0;
}

// start up a job, given a job packet
int process_run_job( struct wish_state* state, struct wish_connection* con, struct wish_job_packet* job ) {
   // create stdin, stdout, and stderr for this process
   wish_state_rlock( state );
   char* tmp_dir = strdup( state->conf.tmp_dir );
   wish_state_unlock( state );
   
   // make stdin
   char* stdin_path = (char*)calloc( strlen(tmp_dir) + 1 + strlen(WISH_STDIN_TEMPLATE) + 1, 1 );
   sprintf( stdin_path, "%s/%s", tmp_dir, WISH_STDIN_TEMPLATE );
   int stdin_fd = mkstemp( stdin_path );
   
   if( stdin_fd < 0 ) {
      errorf("process_run_job: could not open stdin %s, errno = %d\n", stdin_path, -errno );
      free( stdin_path );
      free( tmp_dir );
      wish_process_reply( state, con, PROCESS_TYPE_FAILURE, job->gpid, 0 );
      wish_disconnect( state, con );
      return -errno;
   }
   
   // get stdin and put it into place
   if( job->stdin_url ) {
      int rc = process_get_file( state, con, job, job->stdin_url, stdin_fd );
      if( rc != 0 ) {
         // failure, but try to run anyway
         errorf("process_run_job: could not get stdin from %s\n", job->stdin_url );
         free( stdin_path );
         free( tmp_dir );
         wish_process_reply( state, con, PROCESS_TYPE_FAILURE, job->gpid, 0 );
         wish_disconnect( state, con );
         close( stdin_fd );
         return rc;
      }
      else {
         dbprintf("process_run_job: downloaded stdin %s to %s\n", job->stdin_url, stdin_path );
         lseek( stdin_fd, 0, SEEK_SET );
      }
   }
   
   // file to run...
   char* job_bin_path = NULL;
   int job_bin_fd = -1;
   
   // if we're supposed to spawn a file, get it and put it into place
   if( job->flags & JOB_USE_FILE ) {
      // job->cmd_text is the url on the remote host
      char* name = wish_basename( job->cmd_text, NULL );
      
      job_bin_path = (char*)calloc( strlen(tmp_dir) + 1 + strlen( name ) + strlen(WISH_BINARY_TEMPLATE) + 3, 1 );
      sprintf(job_bin_path, "%s/%s-%s", tmp_dir, name, WISH_BINARY_TEMPLATE );
      free( name );
      
      job_bin_fd = mkstemp( job_bin_path );
      int rc = 0;
      
      if( job_bin_fd > 0 ) {
         rc = process_get_file( state, con, job, job->cmd_text, job_bin_fd );
         if( rc == 0 ) {
            rc = chmod( job_bin_path, 0700 );
            if( rc != 0 ) {
               errorf("chmod %s errno = %d\n", job_bin_path, -errno );
            }
         }
         
         if( rc != 0 ) {
            // failed to download or chmod
            close( job_bin_fd );
            unlink( job_bin_path );
         }
      }
      else {
         rc = -errno;
      }
      
      if( job_bin_fd < 0 || rc != 0 ) {
         
         unlink( stdin_path );
         free( job_bin_path );
         free( stdin_path );
         free( tmp_dir );
         close( stdin_fd );
         wish_process_reply( state, con, PROCESS_TYPE_FAILURE, job->gpid, 0 );
         wish_disconnect( state, con );
         return rc;
      }
      
      // change the command to refer to this binary
      free( job->cmd_text );
      job->cmd_text = (char*)calloc( strlen("exec ") + strlen(job_bin_path) + 1, 1 );
      sprintf(job->cmd_text, "exec %s", job_bin_path );
      
      dbprintf("new command: '%s'\n", job->cmd_text );
      close( job_bin_fd );
   }
   
   
   // make stdout and stderr
   char* stdout_path = (char*)calloc( strlen(tmp_dir) + 1 + strlen(WISH_STDOUT_TEMPLATE) + 1, 1 );
   char* stderr_path = (char*)calloc( strlen(tmp_dir) + 1 + strlen(WISH_STDERR_TEMPLATE) + 1, 1 );
   
   sprintf( stdout_path, "%s/%s", tmp_dir, WISH_STDOUT_TEMPLATE );
   sprintf( stderr_path, "%s/%s", tmp_dir, WISH_STDERR_TEMPLATE );
   
   int stdout_fd = mkstemp( stdout_path );
   int stderr_fd = mkstemp( stderr_path );
   
   if( stdin_fd < 0 || stdout_fd < 0 || stderr_fd < 0 ) {
      errorf("process_run_job: could not create files (stdin = %d, stdout = %d, stderr = %d)\n", stdin_fd, stdout_fd, stderr_fd );
      
      if( stdin_fd > 0 ) {
         close(stdin_fd);
         unlink( stdin_path );
      }
      
      if( stdout_fd > 0 ) {
         close(stdout_fd);
         unlink( stdout_path );
      }
      
      if( stderr_fd > 0 ) {
         close(stderr_fd);
         unlink( stderr_path );
      }
      
      if( job_bin_path ) {
         close( job_bin_fd );
         unlink( job_bin_path );
         free( job_bin_path );
      }
         
      free( stdin_path );
      free( stdout_path );
      free( stderr_path );
      free( tmp_dir );
      
      wish_process_reply( state, con, PROCESS_TYPE_FAILURE, job->gpid, 0 );
      
      wish_disconnect( state, con );
      wish_free_job_packet( job );
      
      return -errno;
   }
   
   struct wish_process* proc = (struct wish_process*)calloc( sizeof(struct wish_process), 1 );
   
   // NOTE: proc and its associated data will be freed by process_writeback_func, which gets used by process_run
   
   // run the process, and send the URLs of our stdout and stderr back to the caller
   int rc = process_run( state, con, job, proc, stdin_fd, stdout_fd, stderr_fd, stdout_path, stderr_path );
   
   // no more need for stdin
   close( stdin_fd );
   unlink( stdin_path );
   free( stdin_path );
   free( tmp_dir );
   
   // no more need for the job binary
   if( job_bin_path ) {
      unlink( job_bin_path );
      free( job_bin_path );
   }
   
   return rc;
}

// pthread bootstrapper for process_run_job
void* process_run_job_pthread( void* arg ) {
   struct process_run_args* args = (struct process_run_args*)arg;
   int rc = process_run_job( args->state, args->con, args->job );
   dbprintf("process_run_job returned %d\n", rc );
   
   wish_free_job_packet( args->job );
   free( args->job );
   free( args );
   
   return NULL;
}


// verify that a file exists and it meets the given access criteria.
static int is_regular_file( char* flatp, int access ) {

   // does this path exist?
   struct stat sb;
   int rc = stat( flatp, &sb );
   if( rc != 0 ) {
      // nope
      errorf("is_regular_file: %s could not be stat'ed\n", flatp);
      return -ENOENT;
   }
   
   if( !S_ISREG( sb.st_mode ) ) {
      // not a regular file
      errorf("is_regular_file: %s is not a regular file\n", flatp );
      return -EISDIR;
   }
   
   if( (sb.st_mode & access) != access ) {
      // missing bits
      errorf("is_regular_file: %s is not accessible with %o\n", flatp, access );
      return -EPERM;
   }
   
   return 0;
}


// determine if a file can be read, given the visibility requirements
static int is_publicly_visible( struct wish_state* state, char* flatp ) {
   // is this in our files root?
   wish_state_rlock( state );
   int rc = 0;
   
   char* files_root = state->conf.files_root;
   
   if( strstr( flatp, files_root ) != flatp ) {
      errorf("is_publicly_visible: %s is not in the files root %s\n", flatp, files_root );
      rc = -EINVAL;
   }
   else {
      for( vector<char*>::iterator itr = state->fs_invisible->begin(); itr != state->fs_invisible->end(); itr++ ) {
         if( strstr( flatp, (*itr) ) == flatp ) {
            errorf("is_publicly_visible: %s is in hidden folder %s\n", flatp, (*itr) );
            rc = -EINVAL;
            break;
         }
      }
   }
   wish_state_unlock( state );
   
   return rc;
}


// determine the url for a file that is publicly visible
static char* public_url( struct wish_state* state, char* flatp ) {
   // rewrite the cmd_text to be a URL to the file
   wish_state_rlock( state );
   
   if( strstr( flatp, state->conf.files_root ) != flatp ) {
      errorf("public_url: %s is not in the files root %s\n", flatp, state->conf.files_root );
      wish_state_unlock( state );
      return NULL;
   }
   
   bool https = state->conf.use_https;
   char* files_root = state->conf.files_root;
   char* hostname = state->hostname;
   int port = state->conf.http_portnum;
   
   char* file_url = (char*)calloc( strlen("https://") + strlen(hostname) + strlen(WISH_HTTP_FILE) + 13 + strlen(flatp) - strlen(files_root) + 1, 1 );
   sprintf(file_url, "%s://%s:%d/%s/%s", (https ? "https" : "http"), hostname, port, WISH_HTTP_FILE, flatp + strlen(files_root) );
   
   wish_state_unlock( state );
   
   return file_url;
}


// make an output file
FILE* make_output( char* path, uid_t user, gid_t group, int umask ) {
   FILE* f = fopen( path, "w" );
   if( f ) {
      int rc = 0;
      // make it so it has the right owner
      int fd = fileno( f );
      rc = fchown( fd, user, group );
      if( rc != 0 ) {
         errorf("make_output: fchown %s errno = %d\n", path, -errno );
      }
      rc = fchmod( fd, (~umask) & 0777 );
      if( rc != 0 ) {
         errorf("make_output: fchmod %s errno = %d\n", path, -errno );
      }
   }
   else {
      errorf("process_spawn: failed to create stdout %s\n", path );
   }
   
   return f;
}


// spawn a running process, but on a remote host.
// record local information on it first.
int process_spawn( struct wish_state* state, struct wish_job_packet* job, struct wish_connection* client_con, uint64_t nid ) {
   // sanity check--make sure this process does not exist
   spawned_rlock();
   if( spawned.find(job->gpid) != spawned.end() ) {
      errorf("process_spawn: process ID collision on %lu\n", job->gpid);
      spawned_unlock();
      return -EEXIST;
   }
   spawned_unlock();
   
   // sanity check--if this nid matches the nid of one of the possible "localhost" nids, then rewrite
   // it to be the nid of the canonical hostname
   for( vector<uint64_t>::size_type i = 0; i < localhost_nids.size(); i++ ) {
      if( localhost_nids[i] == nid ) {
         wish_state_rlock( state );
         nid = state->nid;
         wish_state_unlock( state );
         break;
      }
   }
   
   // sanity check--if we have JOB_USE_FILE as a flag, make sure it's publicly accessible
   if( !(job->flags & JOB_WISH_ORIGIN) && (job->flags & JOB_USE_FILE) ) {
      char* flatp = realpath( job->cmd_text, NULL );
      
      if( flatp == NULL ) {
         // does not exist
         errorf("process_spawn: %s is not a valid path\n", job->cmd_text );
         return -errno;
      }
      
      int rc = is_regular_file( flatp, S_IXUSR );
      if( rc ) {
         errorf("process_spawn: invalid job file %s\n", flatp);
         free( flatp );
         return rc;
      }
      
      rc = is_publicly_visible( state, flatp );
      if( rc ) {
         errorf("process_spawn: inaccessable job file %s\n", flatp );
         free( flatp );
         return rc;
      }
      
      char* file_url = public_url( state, flatp );
      
      free( job->cmd_text );
      job->cmd_text = file_url;
      free( flatp );
   }
   
   // if stdin is given, and this is a spawn command from a client, then rewrite the stdin path to be the stdin url (if possible)
   if( !(job->flags & JOB_WISH_ORIGIN) && job->stdin_url ) {
      char* flatp = realpath( job->stdin_url, NULL );
      
      if( flatp == NULL ) {
         // does not exist
         errorf("process_spawn: %s is not a valid path for stdin\n", job->stdin_url );
         return -errno;
      }
      
      int rc = is_regular_file( flatp, S_IRUSR );
      if( rc ) {
         errorf("process_spawn: invalid stdin file %s\n", flatp);
         free( flatp );
         return rc;
      }
      
      rc = is_publicly_visible( state, flatp );
      if( rc ) {
         errorf("process_spawn: inaccessable stdin file %s\n", flatp );
         free( flatp );
         return rc;
      }
      
      char* file_url = public_url( state, flatp );
      
      free( job->stdin_url );
      job->stdin_url = file_url;
      free( flatp );
   }
   
   // get a connection to this host
   struct wish_connection* con = (struct wish_connection*)calloc( sizeof(struct wish_connection), 1 );
   int rc = heartbeat_get_nid( state, nid, con );
   if( rc != 0 ) {
      errorf("process_spawn: heartbeat_get_nid rc = %d\n", rc );
      return rc;
   }
   printf("spawned process %lu connected on %d\n", job->gpid, con->soc );
   
   // create a job packet with this job's information, but from this host
   struct wish_job_packet jobpkt;
   struct sockaddr_storage addr;
   memset( &addr, 0, sizeof(struct sockaddr_storage) );
   int http_portnum = -1;
   
   wish_state_rlock( state );
   http_portnum = state->conf.http_portnum;
   memcpy( &addr, state->addr->ai_addr, state->addr->ai_addrlen );
   wish_state_unlock( state );
   
   wish_init_job_packet( state, &jobpkt, nid, job->ttl, &addr, 1, job->cmd_text, job->stdin_url, job->flags | JOB_WISH_ORIGIN, job->timeout, http_portnum );
   jobpkt.gpid = job->gpid;
   
   // send off this job
   struct wish_packet pkt;
   wish_pack_job_packet( state, &pkt, &jobpkt );
   
   rc = wish_write_packet( state, con, &pkt );
   wish_free_packet( &pkt );
   wish_free_job_packet( &jobpkt );
   
   if( rc != 0 ) {
      // failed to send
      errorf("process_spawn: wish_write_packet rc = %d\n", rc );
   }
   else {
      // new process...
      struct wish_spawn* new_proc = (struct wish_spawn*)calloc( sizeof(struct wish_spawn), 1 );
      wish_spawned_init( state, new_proc, job );
      new_proc->con = con;
      new_proc->client = client_con;
      
      // attempt to open the stdout and stderr files
      if( job->stdout_path ) {
         new_proc->stdout = make_output( job->stdout_path, job->owner, job->group, job->umask );
      }
      else {
         new_proc->stdout = NULL;
      }
      if( job->stderr_path ) {
         new_proc->stderr = make_output( job->stderr_path, job->owner, job->group, job->umask );
      }
      else {
         new_proc->stderr = NULL;
      }
      
      spawned_wlock();
      spawned[ job->gpid ] = new_proc;
      spawned_unlock();
   }
   
   return rc;
}


// do the join
static int process_do_join( struct wish_state* state, struct wish_spawn** spawn, int type, uint64_t gpid, int exit ) {
   int rc = 0;
   if( (*spawn)->join ) {
      rc = wish_process_reply( state, (*spawn)->join, type, gpid, exit );
      if( rc != 0 ) {
         errorf("process_do_join: could not reply %d to client, rc = %d\n", type, rc );
      }
      else {
         wish_spawned_destroy( state, *spawn );
         free( *spawn );
         *spawn = NULL;
         rc = PROCESS_UPDATE_DESTROYED;
      }
   }
   return rc;
}

// process a process packet (on the originator)
// spawned should be write-locked
// return 0 on success
// return negative on error
// return 1 if the spawned process has died
int process_update( struct wish_state* state, struct wish_process_packet* pkt ) {
   int rc = 0;
   
   if( spawned.find( pkt->gpid ) != spawned.end() ) {
      dbprintf("process_update: packet type %d\n", pkt->type );
      switch( pkt->type ) {
         case PROCESS_TYPE_STARTED: {
            // mark this prcoess as having started up
            spawned[ pkt->gpid ]->start_time = time(NULL);
            spawned[ pkt->gpid ]->status = PROCESS_STATUS_STARTED;
            if( spawned[pkt->gpid]->client ) {
               // pass this along to the client program
               rc = wish_process_reply( state, spawned[pkt->gpid]->client, PROCESS_TYPE_STARTED, pkt->gpid, 0 );
               if( rc != 0 ) {
                  errorf("process_update: could not reply START to client, rc = %d\n", rc );
               }
               
               wish_disconnect( state, spawned[pkt->gpid]->client );
               free( spawned[pkt->gpid]->client = NULL );
               spawned[pkt->gpid]->client = NULL;
            }
            break;
         }
         case PROCESS_TYPE_EXIT: {
            // mark this process as having finished
            spawned[ pkt->gpid ]->status = PROCESS_STATUS_FINISHED;
            spawned[ pkt->gpid ]->exit_code = pkt->data;
            
            if( spawned[pkt->gpid]->flags & JOB_DETACHED ) {
               // no need to wait for join
               wish_spawned_destroy( state, spawned[pkt->gpid] );
               free( spawned[pkt->gpid] );
               spawned[pkt->gpid] = NULL;
               rc = PROCESS_UPDATE_DESTROYED;
            
            }
            else {
               // flush stdout and stderr nevertheless
               if( spawned[pkt->gpid]->stdout )
                  fflush( spawned[pkt->gpid]->stdout );
               if( spawned[pkt->gpid]->stderr )
                  fflush( spawned[pkt->gpid]->stderr );
               
               rc = process_do_join( state, &spawned[pkt->gpid], pkt->type, pkt->gpid, pkt->data );
            }
            
            break;
         }
         case PROCESS_TYPE_ERROR:
         case PROCESS_TYPE_FAILURE: {
            // erase this process--it failed to run
            if( spawned.find( pkt->gpid ) != spawned.end() ) {
               rc = process_do_join( state, &spawned[pkt->gpid], pkt->type, pkt->gpid, pkt->data );
               
               if( rc != PROCESS_UPDATE_DESTROYED ) {
                  wish_spawned_destroy( state, spawned[ pkt->gpid ] );
                  free( spawned[pkt->gpid] );
                  spawned[pkt->gpid] = NULL;
                  rc = PROCESS_UPDATE_DESTROYED;
               }
               errorf("process_update: process %lu has failed\n", pkt->gpid );
            }
            else {
               errorf("process_update: unknown process %lu failed\n", pkt->gpid );
            }
            break;
         }
         case PROCESS_TYPE_TIMEOUT: {
            // erase this process--it timed out
            
            rc = process_do_join( state, &spawned[pkt->gpid], pkt->type, pkt->gpid, pkt->data );
            
            wish_spawned_destroy( state, spawned[ pkt->gpid ] );
            free( spawned[pkt->gpid] );
            spawned[pkt->gpid] = NULL;
            rc = PROCESS_UPDATE_DESTROYED;
            errorf("process_update: process %lu has timed out\n", pkt->gpid );
            break;
         }
         default: {
            errorf("process_update: unknown process packet type %d\n", pkt->type );
            rc = -EINVAL;
            break;
         }
      }
   }
   else {
      errorf("process_update: no process with gpid = %lu\n", pkt->gpid );
      rc = -ENOENT;
   }
   
   return rc;
}

// signal a running process (that is local)
int process_recv_signal( struct wish_state* state, uint64_t gpid, int signal ) {
   int rc = 0;
   
   dbprintf("process_recv_signal: got signal %d for gpid %lu\n", signal, gpid );
   ProcessTable::iterator itr = procs.find( gpid );
   if( itr != procs.end() ) {
      struct wish_process* proc = itr->second;
      rc = kill( proc->pid, signal );
      dbprintf("process_recv_signal: sent signal %d for gpid %lu (pid = %d), rc = %d\n", signal, gpid, proc->pid, rc );
      if( rc != 0 ) {
         rc = -errno;
      }
   }
   else {
      rc = -ENOENT;
   }
   
   return rc;
}

// signal all running process (that are local).
// return number of failures
int process_recv_signal_all( struct wish_state* state, int signal ) {
   int rc = 0;
   
   for( ProcessTable::iterator itr = procs.begin(); itr != procs.end(); itr++ ) {
      struct wish_process* proc = itr->second;
      int kill_rc = kill( proc->pid, signal );
      if( kill_rc != 0 ) {
         rc++;
      }
   }
   
   return rc;
}

// signal a running process (that is remote)
int process_send_signal( struct wish_state* state, uint64_t gpid, int signal ) {
   int rc = 0;
   
   spawned_rlock();
   SpawnTable::iterator itr = spawned.find( gpid );
   if( itr != spawned.end() ) {
      struct wish_process_packet pkt;
      wish_init_process_packet( state, &pkt, PROCESS_TYPE_PSIG, gpid, signal, 0 );
      
      struct wish_packet wpkt;
      wish_pack_process_packet( state, &wpkt, &pkt );
      
      rc = wish_write_packet( state, itr->second->con, &wpkt );
      
      wish_free_packet( &wpkt );
   }
   else {
      rc = -ENOENT;
   }
   spawned_unlock();
   return rc;
}


// send a signal to all processes spawned here
int process_send_signal_all( struct wish_state* state, int signal ) {
   int rc = -ENONET;
   
   spawned_rlock();
   for( SpawnTable::iterator itr = spawned.begin(); itr != spawned.end(); itr++ ) {
      struct wish_process_packet pkt;
      wish_init_process_packet( state, &pkt, PROCESS_TYPE_PSIGALL, itr->second->gpid, signal, 0 );
      
      struct wish_packet wpkt;
      wish_pack_process_packet( state, &wpkt, &pkt );
      
      int write_rc = wish_write_packet( state, itr->second->con, &wpkt );
      if( write_rc != 0 ) {
         errorf("process_send_signal_all: wish_write_packet to %lu rc = %d\n", itr->second->gpid, write_rc );
      }
      
      wish_free_packet( &wpkt );
      
      if( write_rc == 0 ) {
         rc = 0;
         break;
      }
   }
   spawned_unlock();
   return rc;
}

// join on a running process (on the origin).  return 0 on success; negative on error.
int process_join( struct wish_state* state, struct wish_connection* con, uint64_t gpid, bool block ) {
   int rc = 0;
   
   spawned_wlock();
   SpawnTable::iterator itr = spawned.find( gpid );
   if( itr != spawned.end() ) {
      if( itr->second->status == PROCESS_STATUS_FINISHED ) {
         // process already terminated--reply the exit status
         itr->second->join = con;
         rc = process_do_join( state, &itr->second, PROCESS_TYPE_EXIT, gpid, itr->second->exit_code );
         
         if( rc > 0 )
            // this is fine.
            rc = 0;
         
         spawned.erase( gpid );
      }
      else if( block ) {
         // register this connection to be replied to when the process dies
         if( itr->second->join ) {
            wish_disconnect( state, itr->second->join );
            free( itr->second->join );
         }
         itr->second->join = con;
      }
      else {
         // reply that it's still working
         rc = wish_process_reply( state, con, PROCESS_TYPE_ERROR, gpid, -EAGAIN );
      }
   }
   else {
      rc = -ENOENT;
   }
   spawned_unlock();
   
   return rc;
}

// spawn up a thread and run a job with process_run_job (called by executing daemon)
int process_start( struct wish_state* state, struct wish_connection* con, struct wish_job_packet* job ) {
   struct process_run_args* args = (struct process_run_args*)calloc( sizeof(struct process_run_args), 1 );
   args->state = state;
   args->con = con;
   args->job = job;
   
   pthread_attr_t attrs;
   memset( &attrs, 0, sizeof(attrs) );
   
   pthread_attr_setdetachstate( &attrs, 1 );
   
   pthread_t proc_thread;
   memset( &proc_thread, 0, sizeof(proc_thread) );
   
   int rc = pthread_create( &proc_thread, &attrs, process_run_job_pthread, args );
   return rc;
}

// look up the gpid of a process that is executing here.
// return 0 on failure
uint64_t process_get_gpid( struct wish_state* state, pid_t pid ) {
   procs_rlock();
   
   uint64_t ret = 0;
   for( ProcessTable::iterator itr = procs.begin(); itr != procs.end(); itr++ ) {
      if( itr->second->pid == pid ) {
         ret = itr->first;
         break;
      }
   }
   
   procs_unlock();
   
   return ret;
}

