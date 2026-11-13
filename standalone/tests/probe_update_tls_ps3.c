/* Offline emulator probe. No package installation or updater download. */
#include <sys/time.h>
#include <sys/process.h>
#include <stdio.h>
#include "update.h"
#include "update_tls.h"
SYS_PROCESS_PARAM(1001,0x100000);
int main(void) {
    FILE *log=fopen("/dev_hdd0/game/SVIZ00001/USRDIR/tls-probe.log","w");
    if(!log) return 20;
    int network=update_net_init();
    fprintf(log,"network=%d\n",network);fflush(log);
    if(network) {fclose(log);return 21;}
    CURL *curl=curl_easy_init();
    CURLcode code=update_configure_tls(curl,"/dev_hdd0/game/SVIZ00001/USRDIR/certs/cacert.pem");
    if(code==CURLE_OK) {
        curl_easy_setopt(curl,CURLOPT_URL,NL_UPDATE_URL);
        curl_easy_setopt(curl,CURLOPT_NOBODY,1L);
        curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
        curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,6L);
        curl_easy_setopt(curl,CURLOPT_TIMEOUT,12L);
        code=curl_easy_perform(curl);
    }
    long http=0;
    if(curl) curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&http);
    fprintf(log,"backend=%s\ncurl=%d http=%ld\n",curl_version(),(int)code,http);
    if(curl) curl_easy_cleanup(curl);
    update_net_shutdown();fclose(log);
    return code==CURLE_OK && http==200?0:22;
}
