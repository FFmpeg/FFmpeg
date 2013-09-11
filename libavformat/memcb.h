#ifndef AVFORMAT_MEMCB_H
#define AVFORMAT_MEMCB_H

typedef struct MemCallBackContext {
    void (*url_init)(struct MemCallBackContext *h);
    //return 0 = EAGAIN >0 bytes <0 error
    int (*url_read)(struct MemCallBackContext *h, unsigned char *buf, int size);
    //return 0 = success !0 error
    int (*url_write)(struct MemCallBackContext *h, const unsigned char *buf, int size);
    void (*url_free)(struct MemCallBackContext *h);
    //read write flag
    int flags;
    //user private data
    void *priv_data;
}MemCallBackContext;

//不需要delete
MemCallBackContext *memcb_new();

void memcb_geturl(MemCallBackContext *ctx,char *pathbuf);

#endif /* AVFORMAT_MEMCM_H */

