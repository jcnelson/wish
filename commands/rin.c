
// rin.c


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void PrintUsage(int argc, char *argv[]) {
    if (argc >=1) {
        printf("Usage: %s -h -f NID:STREAM COMMAND|GUPID\n", argv[0]);
	printf("  NID: Node ID\n");
	printf("  STREAM: File Stream\n");
	printf("  Command: Program to redirect to\n");
	printf("  GUPID: Process to redirect to\n");
	printf("\n  Options:\n");
        printf("      -f\tRead stream from file, where STREAM is path to file\n");
        printf("      -h\tShow this help screen.\n");
        printf("\n");
    }
}
 
int main(int argc, char *argv[]) {


    int c;
    int file = 0;

    while( (c = getopt(argc, argv, "hf")) != -1) {
        switch(c){
            case 'f':
                file = 1;
                break;
            case 'h':
                PrintUsage(argc, argv);
                exit(0);
                break;
            default:
                PrintUsage(argc, argv);
                exit(0);
                break;
        }
    }


    if ( (argc == 3 && optind != 1) || (argc == 4 && optind != 2) || argc < 3 || argc > 4){
        PrintUsage(argc,argv);
        exit(0);
    }

    char* command1;
    char* command2;  
    command1 = strdup(argv[optind]);
    command2 = strdup(argv[optind+1]);
    char* pch;
    int tok = 0;
    int nid;
    int command_flag = 1;
    char* stream;

    pch = strtok (command1, ":");
    while (pch != NULL)
    {
        if( tok == 0){
            if(sscanf(pch, "%d", &nid) != 1)
            {
                PrintUsage(argc,argv);
                exit(0);
            }
	}
        if( tok == 1) stream = strdup(pch);
        pch = strtok (NULL, ":");
        tok++;
    } 

    if( tok != 2){
        PrintUsage(argc,argv);
        exit(0);
    }
   
    printf("%d %s\n",nid,stream);

    long gupid;

    if(sscanf(command2, "%ld", &gupid) == 1) command_flag = 0;


}
