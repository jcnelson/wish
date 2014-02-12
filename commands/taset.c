// taset.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libwish.h"

int main(int argc, char *argv[])
{
   if(argc < 4)
   {
            fprintf(stderr, "%s NAME VALUE CMP\n", argv[0]);
            exit(1);
   }

   // read the config file
                                                                                                                                                      
   struct wish_conf conf;
   int rc = wish_read_conf( WISH_DEFAULT_CONFIG, &conf );
   if( rc != 0 ) {
      fprintf(stderr, "Config file %s could not be read\n", WISH_DEFAULT_CONFIG );
      exit(1);
   }

   char * wishHost = getenv("WISH_ORIGIN");
   if(wishHost == NULL)
   {
      wishHost = strdup("localhost");
   }

   char * portNumber = getenv("WISH_HTTP_PORTNUM");
   if(portNumber == NULL)
   {
      portNumber = (char *) calloc(11, 1);
      sprintf(portNumber, "%d", conf.http_portnum);
   }

   int len = strlen("http://:/TASET/-,") + strlen(wishHost) + strlen(portNumber) +
   strlen(argv[1]) + strlen(argv[2]) + strlen(argv[3]) + 1;

   char * url = (char *) calloc(len, 1);

   sprintf(url, "http://%s:%s/TASET/%s=%s,%s", wishHost, portNumber, argv[1], argv[2], argv[3]);

   struct wish_HTTP_info resp;
   int fd = -1; // don't store anything to disk
   int res = wish_HTTP_download_file(NULL, &resp, url, NULL, NULL, fd); // returns 0 if curl didn't break, resp.status = 200 if it went fine           
   if(res != 0 || resp.status != 200)
      printf("Error: taset didn't work! rc = %d resp.status = %d\n", res, resp.status);

   return 0;
}

