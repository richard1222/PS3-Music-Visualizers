#include "update_tls.h"
#include <stdio.h>
#include <stdlib.h>
static size_t discard(void *data,size_t size,size_t count,void *user) {
    (void)data;(void)user;return size*count;
}
int main(int argc,char **argv) {
    if(argc!=4) return 2;
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK) return 3;
    CURL *curl=curl_easy_init();
    if(!curl) {curl_global_cleanup();return 4;}
    CURLcode result=update_configure_tls(curl,argv[2]);
    if(result==CURLE_OK) {
        curl_easy_setopt(curl,CURLOPT_URL,argv[1]);
        curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
        curl_easy_setopt(curl,CURLOPT_PROXY,"");
        curl_easy_setopt(curl,CURLOPT_TIMEOUT,5L);
        curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,discard);
        result=curl_easy_perform(curl);
    }
    curl_easy_cleanup(curl);curl_global_cleanup();
    printf("TLS result=%d expected=%s\n",(int)result,argv[3]);
    return (int)result==atoi(argv[3])?0:1;
}
