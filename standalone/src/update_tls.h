#ifndef NL_UPDATE_TLS_H
#define NL_UPDATE_TLS_H
#include <curl/curl.h>
/* Shared with the local HTTPS integration test. Never retry insecurely. */
static inline CURLcode update_configure_tls(CURL *curl,const char *ca_bundle) {
    CURLcode rc;
    if(!curl || !ca_bundle || !*ca_bundle) return CURLE_BAD_FUNCTION_ARGUMENT;
    rc=curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
    if(rc!=CURLE_OK) return rc;
    rc=curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
    if(rc!=CURLE_OK) return rc;
    rc=curl_easy_setopt(curl,CURLOPT_CAINFO,ca_bundle);
    if(rc!=CURLE_OK) return rc;
    rc=curl_easy_setopt(curl,CURLOPT_PROTOCOLS,(long)CURLPROTO_HTTPS);
    if(rc!=CURLE_OK) return rc;
    rc=curl_easy_setopt(curl,CURLOPT_REDIR_PROTOCOLS,(long)CURLPROTO_HTTPS);
    if(rc!=CURLE_OK) return rc;
    return curl_easy_setopt(curl,CURLOPT_MAXREDIRS,5L);
}
#endif
