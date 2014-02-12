
// dctl.c


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void PrintUsage(int argc, char *argv[]) {
    if (argc >=1) {
        printf("Usage: %s -h start|stop|restart|reset\n", argv[0]);
	printf("\n  Options:\n");
        printf("      -h\tShow this help screen.\n");
        printf("\n");
    }
}
 
int main(int argc, char *argv[]) {

    char* command;   

    if( argc != 2){
        PrintUsage(argc,argv);
        exit(0);
    }

    command = strdup (argv[1]);

    if( strcmp(command,"start") == 0 )  printf("start\n"); 
    else if( strcmp(command,"stop") == 0 ) printf("stop\n");
    else if( strcmp(command,"restart") == 0 ) printf("restart\n");
    else if( strcmp(command,"reset") == 0 ) printf("reset\n");
    else{
        PrintUsage(argc,argv);
        exit(0);
    }

}

