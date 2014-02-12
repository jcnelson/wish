#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include "libwish.h"

 
#define DAEMON_NAME "wlistend"
#define PID_FILE "/var/run/wlistend.pid"

int eflag;
 
/**************************************************************************
    Function: Print Usage
 
    Description:
        Output the command-line options for this daemon.
 
    Params:
        @argc - Standard argument count
        @argv - Standard argument array
 
    Returns:
        returns void always
**************************************************************************/
void PrintUsage(int argc, char *argv[]) {
    if (argc >=1) {
        printf("Usage: %s -h -n\n", argv[0]);
        printf("  Options:\n");
        printf("      -n\tDon't fork off as a daemon.\n");
        printf("      -h\tShow this help screen.\n");
        printf("\n");
    }
}
 
/**************************************************************************
    Function: signal_handler
 
    Description:
        This function handles select signals that the daemon may
        receive.  This gives the daemon a chance to properly shut
        down in emergency situations.  This function is installed
        as a signal handler in the 'main()' function.
 
    Params:
        @sig - The signal received
 
    Returns:
        returns void always
**************************************************************************/
void signal_handler(int sig) {
 
    switch(sig) {
        case SIGHUP:
            printf( "Received SIGHUP signal.\n");
            eflag = 0;
	    break;
        case SIGTERM:
            printf("Received SIGTERM signal.\n");
            eflag = 0;
            break;
        default:
            printf("Unhandled signal (%d) %s\n",sig,strsignal(sig));
            eflag = 0;
            break;
    }
}
 
/**************************************************************************
    Function: main
 
    Description:
        The c standard 'main' entry point function.
 
    Params:
        @argc - count of command line arguments given on command line
        @argv - array of arguments given on command line
 
    Returns:
        returns integer which is passed back to the parent process
**************************************************************************/
int main(int argc, char *argv[]) {

    eflag = 1;

#if defined(DEBUG)
    int daemonize = 0;
#else
    int daemonize = 1;
#endif
 
    // Setup signal handling before we start
    signal(SIGHUP, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGQUIT, signal_handler);
 
    int c;
    while( (c = getopt(argc, argv, "nh|help")) != -1) {
        switch(c){
            case 'h':
                PrintUsage(argc, argv);
                exit(0);
                break;
            case 'n':
                daemonize = 0;
                break;
            default:
                PrintUsage(argc, argv);
                exit(0);
                break;
        }
    }
 
    printf("%s daemon starting up\n", DAEMON_NAME);
 
    /* Our process ID and Session ID */
    pid_t pid, sid;
 
    if (daemonize) {
        printf("starting the daemonizing process\n");
 
        /* Fork off the parent process */
        pid = fork();
        if (pid < 0) {
            exit(EXIT_FAILURE);
        }
        /* If we got a good PID, then
           we can exit the parent process. */
        if (pid > 0) {
            exit(EXIT_SUCCESS);
        }
 
        /* Change the file mode mask */
        umask(0);
 
        /* Create a new SID for the child process */
        //sid = setsid();
        //if (sid < 0) {
        //    /* Log the failure */
        //    exit(EXIT_FAILURE);
        //}
 
        /* Change the current working directory */
        if ((chdir("/")) < 0) {
            /* Log the failure */
            exit(EXIT_FAILURE);
        }
 
        /* Close out the standard file descriptors */
        //close(STDIN_FILENO);
        //close(STDOUT_FILENO);
        //close(STDERR_FILENO);
    }
 

    printf("Daemon test\n");
 
    struct wish_conf conf;
    memset( &conf, 0, sizeof(wish_conf) );
 
    // read config
    int rc = wish_read_conf( WISH_DEFAULT_CONFIG, &conf);
    if( rc != 0 ) {
       errorf("wish_read_conf rc = %d\n", rc );
       exit(1);
    }

    printf("Port: %d \n",conf.portnum);   

    // connect to the daemon
    struct wish_connection con;
    rc = wish_connect( NULL , &con, "localhost", conf.portnum );
    if( rc < 0 ) {
       errorf("wish_connect: rc = %d\n", rc );
       exit(1);
    }

    close(0);
     // force infinite timeout
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
   
    rc = setsockopt( con.soc, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv) );
    if( rc != 0 ) {
       fprintf(stderr, "setsockopt errno = %d\n", -errno );
       exit(1);
    }

    struct wish_packet wp;
    memset( &wp, 0, sizeof(wp) );
    
    wp.hdr.type = 777;
    wp.hdr.payload_len = 0;  
 
    rc = wish_write_packet( NULL, &con, &wp );
    if( rc < 0 ) {
       errorf("wish_write_packet: rc = %d\n", rc );
       exit(1);
    }

    struct wish_packet reply;
    struct wish_strings_packet wssp;  
    int i; 

    while(eflag){


		rc = wish_read_packet( NULL, &con, &reply );
		
		if( rc < 0 ) {
			errorf("wish_read_packet: rc = %d\n", rc );
			exit(1);
		}

		// unmarshal the reply
		if( reply.hdr.type == PACKET_TYPE_STRINGS ) {
			wish_unpack_strings_packet( NULL, &reply, &wssp );
			for( i = 0 ; i < wssp.count ; i++){
				printf("%s",wssp.packets[i].str);

			}
			wish_free_strings_packet(&wssp);		
		
		}
		else {
			printf("UNKNOWN REPLY %d\n", reply.hdr.type );
		}

	}


	// close the connection
	wish_disconnect( NULL, &con );

	// free memory
	wish_free_packet( &reply );


	printf("%s daemon exiting\n", DAEMON_NAME);

	//****************************************************
	// TODO: Free any allocated resources before exiting
	//****************************************************

	exit(0);
	}
