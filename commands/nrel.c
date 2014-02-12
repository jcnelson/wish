
// nrel.c


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void PrintUsage(int argc, char *argv[]) {
    if (argc >=1) {
        printf("Usage: %s -h NID\n", argv[0]);
	printf("\n  NID: Node ID\n");
	printf("\n  Options:\n");
        printf("      -h\tShow this help screen.\n");
        printf("\n");
    }
}
 
int main(int argc, char *argv[]) {

    int nid;
   
    if( argc != 2){
        PrintUsage(argc,argv);
        exit(0);
    }

    if(sscanf(argv[1], "%d", &nid) != 1)
    {
        PrintUsage(argc,argv);
        exit(0);
    }
    
    printf("%d\n",nid);

}

