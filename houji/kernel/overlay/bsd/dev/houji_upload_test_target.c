/* Explicit opt-in small RAM object. Not a root volume; never mounted or saved. */
#include <sys/systm.h>
#include "houji_ram_upload.h"

static uint8_t test_bytes[65560];
static int test_write(void *context __unused,uint64_t off,const uint8_t *p,unsigned n)
{
    if(off>sizeof(test_bytes) || n>sizeof(test_bytes)-off) return -1;
    memcpy(test_bytes+off,p,n);return 0;
}
static int test_read(void *context __unused,uint64_t off,uint8_t *p,unsigned n)
{
    if(off>sizeof(test_bytes) || n>sizeof(test_bytes)-off) return -1;
    memcpy(p,test_bytes+off,n);return 0;
}
void houji_ram_upload_bind_test_target(void);
void houji_ram_upload_bind_test_target(void)
{
    struct hru_target target={
        .bytes=sizeof(test_bytes),
        .sha256={0x84,0xe0,0xc4,0x6f,0x00,0x66,0x96,0x86,0x05,0xbf,0x12,0xe7,0x72,0x3d,0xc8,0x51,
                 0x27,0x5e,0xe8,0xca,0x05,0xfc,0x71,0xb9,0x44,0xf5,0x73,0xcb,0xb2,0x20,0xfa,0x02},
        .context=NULL,.write=test_write,.read=test_read,.emit=NULL
    };
    int result=houji_ram_upload_bind(&target);
    printf("HOUJI UPLOAD: small test target bind=%d bytes=%lu; never mounted\n",result,(unsigned long)sizeof(test_bytes));
}
