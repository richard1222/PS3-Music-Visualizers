#include "update.h"
#include <stdio.h>
#include <string.h>
static int fails;
#define CHECK(cond) do { if(!(cond)) { printf("FAIL line %d: %s\n",__LINE__,#cond); fails++; } } while(0)
static void write_tmp(const char *name,const char *data) {
    FILE *f=fopen(name,"wb");
    if(data) fwrite(data,1,strlen(data),f);
    fclose(f);
}
int main(void) {
    int major,minor;
    UpdateManifest man;
    char hex[65];
    CHECK(update_parse_version("1.00",&major,&minor)==0 && major==1 && minor==0);
    CHECK(update_parse_version("2.10",&major,&minor)==0 && major==2 && minor==10);
    CHECK(update_parse_version("1",&major,&minor)!=0);
    CHECK(update_parse_version("1.",&major,&minor)!=0);
    CHECK(update_parse_version("a.b",&major,&minor)!=0);
    CHECK(update_parse_version("",&major,&minor)!=0);
    CHECK(update_parse_version("1.00x",&major,&minor)!=0);
    CHECK(update_is_newer("1.04")==1);
    CHECK(update_is_newer("1.03")==0);
    CHECK(update_is_newer("1.02")==0);
    CHECK(update_is_newer("1.01")==0);
    CHECK(update_is_newer("1.00")==0);
    CHECK(update_is_newer("0.99")==0);
    CHECK(update_is_newer("2.00")==1);
    CHECK(update_is_newer("bad")==0);
    CHECK(update_parse_manifest("{\"version\":\"1.03\",\"pkg_url\":\"https://example.com/neon_lounge.pkg\",\"pkg_sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}",&man)==0);
    CHECK(!strcmp(man.version,"1.03") && !strcmp(man.pkg_url,"https://example.com/neon_lounge.pkg"));
    CHECK(update_parse_manifest("{\n  \"pkg_sha256\" : \"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\" ,\n  \"pkg_url\":\"https://example.com/neon_lounge.pkg\",\n  \"version\":\"1.03\"\n}",&man)==0 && !strcmp(man.version,"1.03"));
    CHECK(update_parse_manifest("{\"version\":\"1.03\",\"pkg_url\":\"https://example.com/neon_lounge.pkg\"}",&man)==UPDATE_ERR_INCOMPLETE);
    CHECK(update_parse_manifest("{\"version\":\"1.03\",\"pkg_sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}",&man)==UPDATE_ERR_INCOMPLETE);
    CHECK(update_parse_manifest("{\"version\":\"1.01\",\"eboot_url\":\"https://example.com/EBOOT.BIN\",\"eboot_sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}",&man)==UPDATE_ERR_INCOMPLETE);
    CHECK(update_parse_manifest("{\"version\":\"1.03\",\"eboot_url\":\"https://example.com/EBOOT.BIN\",\"eboot_sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"pkg_url\":\"https://example.com/neon_lounge.pkg\",\"pkg_sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}",&man)==0 && !strcmp(man.pkg_url,"https://example.com/neon_lounge.pkg"));
    CHECK(update_parse_manifest("{\"version\":\"1.03\",\"pkg_url\":\"http://example.com/neon_lounge.pkg\",\"pkg_sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}",&man)!=0);
    CHECK(update_parse_manifest("{\"version\":\"1.03\",\"pkg_url\":\"https://example.com/neon_lounge.pkg\",\"pkg_sha256\":\"abcd\"}",&man)!=0);
    CHECK(update_parse_manifest("{\"version\":\"x\",\"pkg_url\":\"https://example.com/neon_lounge.pkg\",\"pkg_sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}",&man)!=0);
    write_tmp("test_update_empty.bin",NULL);
    CHECK(update_sha256_file("test_update_empty.bin",hex)==0);
    CHECK(!strcmp(hex,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    write_tmp("test_update_abc.bin","abc");
    CHECK(update_sha256_file("test_update_abc.bin",hex)==0);
    CHECK(!strcmp(hex,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    write_tmp("test_update_56.bin","abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    CHECK(update_sha256_file("test_update_56.bin",hex)==0);
    CHECK(!strcmp(hex,"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    {
        FILE *f=fopen("test_update_1m.bin","wb");
        for(int i=0;i<1000000;i++) fputc('a',f);
        fclose(f);
    }
    CHECK(update_sha256_file("test_update_1m.bin",hex)==0);
    CHECK(!strcmp(hex,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    CHECK(update_sha256_file("test_update_missing.bin",hex)!=0);
    remove("test_update_empty.bin");
    remove("test_update_abc.bin");
    remove("test_update_56.bin");
    remove("test_update_1m.bin");
    if(fails) { printf("Update tests: %d failures\n",fails); return 1; }
    printf("Update: version compare, manifest parsing and SHA-256 checks passed\n");
    return 0;
}
