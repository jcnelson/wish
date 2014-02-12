// ggetenv.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libwish.h"

int main(int argc, char *argv[])
{
   if(argc < 2)
   {
      fprintf(stderr, "Usage: %s NAME\n", argv[0]);
      exit(1);
   }

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
      
   int len = strlen("http://:/GETENV/") + strlen(wishHost) + strlen(portNumber) + strlen(argv[1]);

   char * url = (char *) calloc(len + 1, 1);

   sprintf(url, "http://%s:%s/GETENV/%s", wishHost, portNumber, argv[1]);

   struct wish_HTTP_info resp;

   struct wish_HTTP_buf buf;
   wish_make_HTTP_buf( &buf, WISH_MAX_ENVAR_SIZE + 1 );
   buf.size = WISH_MAX_ENVAR_SIZE;

   int res = wish_HTTP_download_ram( NULL, &resp, url, NULL, NULL, &buf );
   if(res != 0 || resp.status != 200)
      printf("Error: ggetenv didn't work! rc = %d resp.status = %d\n", res, resp.status);

   else
      printf("%s\n", buf.data);

   wish_free_HTTP_buf( &buf );

   return 0;
}

