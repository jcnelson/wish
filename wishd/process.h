#ifndef _PROCESS_H_
#define _PROCESS_H_

#include "libwish.h"
#include "http.h"
#include "heartbeat.h"
#include <map>

using namespace std;

#define PROCESS_STATUS_STARTED   2        // process has been started
#define PROCESS_STATUS_FINISHED  3        // process has finished
#define PROCESS_STATUS_INIT      1        // process has been initialized
#define PROCESS_STATUS_ERROR     -1       // process has entered an erroneous state

#define PROCESS_STATUS_UNKNOWN   0

#define PROCESS_READ_SIZE 4096

#define PROCESS_UPDATE_DESTROYED 1

// running process info.
// contains information about processes running locally.
struct wish_process {
   uint64_t gpid;                // WISH-wide pid
   pid_t pid;                    // the PID of the process running locally
   struct wish_connection* con;  // connection to the originator
   int stdout_fd;                // fd to stdout on disk
   int stderr_fd;                // fd to stderr on disk
   char* stdout_path;            // path to stdout
   char* stderr_path;            // path to stderr
   time_t expire;                // when this process should expire (-1 for never)
   bool finished;                // set to true once the process terminates
};

// spawned process info
// contains information about processes spawned locally.
struct wish_spawn {
   uint64_t gpid;                // WISH-wide pid
   struct wish_connection* con;  // connection to the daemon running the process
   struct wish_connection* client;  // connection to the client program that spawned the process
   struct wish_connection* join;    // connection to the client program that wants to join with this process
   time_t start_time;            // when did we spawn the process?
   time_t timeout;               // how long until we can kill this process due to timeout
   int status;                   // what state is the process known to be in?
   uint32_t flags;               // process properties
   int exit_code;                // process's exit code
   FILE* stdout;                 // file stream to the process's stdout
   FILE* stderr;                 // file stream to the process's stderr
};

struct process_run_args {
   struct wish_state* state;
   struct wish_connection* con;
   struct wish_job_packet* job;
};


// initialize processes
int process_init( struct wish_state* state );

// shut down processes
int process_shutdown( struct wish_state* state );

// start a process (called by an origin daemon to send off a process)
int process_spawn( struct wish_state* state, struct wish_job_packet* job, struct wish_connection* con, uint64_t nid);

// update the status of a process (called by an origin daemon as it receives status updates from a remote executor)
int process_update( struct wish_state* state, struct wish_process_packet* pkt );

// start a job (called by the executing wish daemon's main loop)
int process_start( struct wish_state* state, struct wish_connection* con, struct wish_job_packet* job );

// synchronously start up and run a job, given a job packet and a connection to the caller (called by an executing daemon to run a process)
// on success, run the job, and send back the results to the caller.
int process_run_job( struct wish_state* state, struct wish_connection* con, struct wish_job_packet* job );

// signal a running process (called on an executing daemon)
int process_recv_signal( struct wish_state* state, uint64_t gpid, int signal );

// signal all running processes (called on an executing daemon)
int process_recv_signal_all( struct wish_state* state, int signal );

// signal a running process (called on an origin daemon)
int process_send_signal( struct wish_state* state, uint64_t gpid, int signal );

// signal all processes spawned here (called on an origin daemon)
// return the number of failed signals
int process_send_signal_all( struct wish_state* state, int signal );

// join on a running process (called on an origin daemon).   return 0 on success; negative on error.
// only gets called on an origin daemon.
int process_join( struct wish_state* state, struct wish_connection* con, uint64_t gpid, bool block );

// reply a process packet
int wish_process_reply( struct wish_state* state, struct wish_connection* con, int type, uint64_t gpid, int data );

// translate a local PID to the GPID of a process this daemon is running (called on the executing daemon)
uint64_t process_get_gpid( struct wish_state* state, pid_t pid );

#define WISH_STDIN_TEMPLATE  ".wish-stdin-XXXXXX"
#define WISH_STDOUT_TEMPLATE ".wish-stdout-XXXXXX"
#define WISH_STDERR_TEMPLATE ".wish-stderr-XXXXXX"
#define WISH_BINARY_TEMPLATE "wish-bin-XXXXXX"

#endif