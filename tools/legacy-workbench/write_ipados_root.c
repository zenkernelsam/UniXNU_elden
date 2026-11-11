/* Fixed-target SHNGROOT installer. Never writes userdata or GPT.
 * Check current GPT, kernel partition mapping, identity and capacity before
 * opening ipados for writes. Stream payload; verify it before committing header.
 */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <openssl/sha.h>
#define PART_BYTES 137439981568ULL
#define FIRST 215283456ULL
#define LAST 248838138ULL
#define DISK_BLOCKS 248838144ULL
#define IMAGE_BYTES 9381609472ULL
#define IMAGE_SHA "ae3c0e19e224c01a2291d1cd58da337062fd1240a3a089af1ccead4f591884c8"
#define HEADER_SHA "6684c652b85ecaa80769c8ec37eb5c4cec5100851b592a55e5f2077897621eab"
#define ENTRY_SHA "acd91e5236f9662bd2202c2afbe05556df9bbfde92b97a8bb89d6c6ee5996f21"
#define CHUNK (1024*1024)
static void fail(const char *s){fprintf(stderr,"REFUSED/FAILED: %s (errno=%d)\n",s,errno);exit(1);}
static uint32_t u32(const void *p){uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t u64(const void *p){uint64_t v;memcpy(&v,p,8);return v;}
static uint32_t crc32(const void *buf,size_t len){
    const unsigned char *p=buf;uint32_t c=~0U;
    while(len--){c^=*p++;for(int i=0;i<8;i++)c=(c>>1)^(0xedb88320U&-(c&1));}return ~c;
}
static int digest_ok(const unsigned char *d,const char *expect){
    char hex[65];for(int i=0;i<32;i++)sprintf(hex+2*i,"%02x",d[i]);return !strcmp(hex,expect);
}
static int hash_ok(const void *p,size_t n,const char *expect){unsigned char d[32];SHA256(p,n,d);return digest_ok(d,expect);}
static void read_at(int fd,void *p,size_t n,uint64_t off){
    while(n){ssize_t r=pread(fd,p,n,off);if(r<0&&errno==EINTR)continue;if(r<=0)fail("pread");p=(char*)p+r;n-=r;off+=r;}
}
static void write_at(int fd,const void *p,size_t n,uint64_t off){
    if(off>=IMAGE_BYTES||n>IMAGE_BYTES-off)fail("write boundary");
    while(n){ssize_t r=pwrite(fd,p,n,off);if(r<0&&errno==EINTR)continue;if(r<=0)fail("pwrite");p=(const char*)p+r;n-=r;off+=r;}
}
static uint64_t input_bytes;
static void input(void *p,size_t n){
    while(n){ssize_t r=read(0,p,n);if(r<0&&errno==EINTR)continue;
        if(r<=0){fprintf(stderr,"Input ended at %llu bytes; result=%zd\n",(unsigned long long)input_bytes,r);fail("truncated input");}
        input_bytes+=r;p=(char*)p+r;n-=r;}
}
static uint64_t number(const char *path){
    FILE *f=fopen(path,"r");unsigned long long n;if(!f||fscanf(f,"%llu",&n)!=1)fail("sysfs number");fclose(f);return n;
}
static void header_ok(unsigned char *h,uint64_t self,uint64_t other,uint64_t table){
    if(memcmp(h,"EFI PART",8)||u32(h+8)!=0x10000||u32(h+12)!=92||u32(h+20)||
       u64(h+24)!=self||u64(h+32)!=other||u64(h+40)>FIRST||u64(h+48)!=LAST||
       u64(h+72)!=table||u32(h+80)!=32||u32(h+84)!=128)fail("GPT header geometry");
    unsigned char copy[92];memcpy(copy,h,92);memset(copy+16,0,4);
    if(crc32(copy,92)!=u32(h+16))fail("GPT header CRC");
}
static void target_ok(int fd){
    struct stat st;uint64_t n;int sector;
    if(fstat(fd,&st)||!S_ISBLK(st.st_mode)||ioctl(fd,BLKGETSIZE64,&n)||n!=PART_BYTES||
       ioctl(fd,BLKSSZGET,&sector)||sector!=4096)fail("partition type/capacity/sector");
    char sys[64],resolved[PATH_MAX],path[PATH_MAX],diskpath[PATH_MAX];
    snprintf(sys,sizeof(sys),"/sys/dev/block/%u:%u",major(st.st_rdev),minor(st.st_rdev));
    if(!realpath(sys,resolved))fail("partition sysfs");
    snprintf(path,sizeof(path),"%s/start",sys);if(number(path)!=FIRST*8)fail("partition start");
    snprintf(path,sizeof(path),"%s/size",sys);if(number(path)!=PART_BYTES/512)fail("partition size");
    snprintf(path,sizeof(path),"%s/partition",sys);if(number(path)!=17)fail("partition index");
    snprintf(path,sizeof(path),"%s/holders",sys);DIR *d=opendir(path);struct dirent *e;
    if(!d)fail("holders");
    while((e=readdir(d)))if(e->d_name[0]!='.')fail("partition has holders");
    closedir(d);
    char *end=strrchr(resolved,'/');if(!end)fail("sysfs parent");*end=0;
    char *parent=strrchr(resolved,'/');if(!parent||strncmp(parent+1,"sd",2))fail("not UFS sd parent");
    snprintf(diskpath,sizeof(diskpath),"/dev/block/%s",parent+1);
    int disk=open(diskpath,O_RDONLY|O_CLOEXEC);struct stat ds;
    if(disk<0||fstat(disk,&ds)||!S_ISBLK(ds.st_mode)||ioctl(disk,BLKGETSIZE64,&n)||
       n!=DISK_BLOCKS*4096||ioctl(disk,BLKSSZGET,&sector)||sector!=4096)fail("LUN0 capacity");
    unsigned char h[4096],b[4096],t[4096],bt[4096];
    read_at(disk,h,4096,4096);read_at(disk,b,4096,(DISK_BLOCKS-1)*4096);
    header_ok(h,1,DISK_BLOCKS-1,2);header_ok(b,DISK_BLOCKS-1,1,DISK_BLOCKS-5);
    if(memcmp(h+56,b+56,16)||u64(h+40)!=u64(b+40))fail("GPT pair");
    read_at(disk,t,4096,8192);read_at(disk,bt,4096,(DISK_BLOCKS-5)*4096);close(disk);
    if(memcmp(t,bt,4096)||crc32(t,4096)!=u32(h+88)||crc32(bt,4096)!=u32(b+88))fail("GPT tables");
    if(!hash_ok(t+16*128,128,ENTRY_SHA))fail("ipados GPT identity");
    if(u64(t+15*128+32)!=5972392ULL||u64(t+15*128+40)!=FIRST-1)fail("userdata boundary");
    for(int i=0;i<32;i++){
        unsigned char zero[16]={0},*p=t+i*128;if(i==16||!memcmp(p,zero,16))continue;
        uint64_t first=u64(p+32),last=u64(p+40);
        if(first>last||(first<=LAST&&last>=FIRST))fail("GPT overlapping entry");
    }
    unsigned char existing[4096];read_at(fd,existing,sizeof(existing),0);
    if(!memcmp(existing,"SHNGROOT",8))fail("existing root image; refuse replacement");
    printf("TARGET VERIFIED %s partition17 LBA%llu..%llu bytes%llu\n",diskpath,FIRST,LAST,PART_BYTES);fflush(stdout);
}
static void verify_payload(int fd,const unsigned char *header,unsigned char *buf,int synthetic){
    SHA256_CTX c;SHA256_Init(&c);unsigned char d[32];
    if(synthetic)SHA256_Update(&c,header,4096);
    for(uint64_t at=synthetic?4096:0;at<IMAGE_BYTES;){
        size_t n=IMAGE_BYTES-at<CHUNK?IMAGE_BYTES-at:CHUNK;read_at(fd,buf,n,at);SHA256_Update(&c,buf,n);at+=n;
    }
    SHA256_Final(d,&c);if(!digest_ok(d,IMAGE_SHA))fail("device image SHA256");
}
int main(int argc,char **argv){
    if(argc!=2||(strcmp(argv[1],"--check")&&strcmp(argv[1],"--write-image")))return 2;
    int writing=!strcmp(argv[1],"--write-image");
    int fd=open("/dev/block/by-name/ipados",(writing?O_RDWR|O_EXCL:O_RDONLY)|O_CLOEXEC);
    if(fd<0)fail("open ipados");
    target_ok(fd);if(!writing){close(fd);return 0;}
    unsigned char h[4096],d[32],extra;unsigned char *buf=malloc(CHUNK);if(!buf)fail("malloc");
    input(h,4096);if(!hash_ok(h,4096,HEADER_SHA)||memcmp(h,"SHNGROOT",8)||u64(h+16)!=IMAGE_BYTES)fail("input header");
    puts("Input header verified; starting payload");fflush(stdout);
    SHA256_CTX c;SHA256_Init(&c);SHA256_Update(&c,h,4096);uint64_t progress=0;
    for(uint64_t at=4096;at<IMAGE_BYTES;){
        size_t n=IMAGE_BYTES-at<CHUNK?IMAGE_BYTES-at:CHUNK;
        input(buf,n);SHA256_Update(&c,buf,n);write_at(fd,buf,n,at);at+=n;
        if(at/(256ULL<<20)!=progress){progress=at/(256ULL<<20);printf("Written %llu / %llu\n",(unsigned long long)at,IMAGE_BYTES);fflush(stdout);}
    }
    ssize_t r;do{r=read(0,&extra,1);}while(r<0&&errno==EINTR);if(r!=0)fail("trailing data/input error");
    SHA256_Final(d,&c);if(!digest_ok(d,IMAGE_SHA))fail("input SHA256");
    if(fsync(fd)||ioctl(fd,BLKFLSBUF,0))fail("payload flush/invalidate");
    puts("Verifying complete payload before header commit");fflush(stdout);
    verify_payload(fd,h,buf,1);write_at(fd,h,4096,0);if(fsync(fd))fail("header fsync");
    if(ioctl(fd,BLKFLSBUF,0))fail("committed image invalidate");
    puts("Header committed; verifying complete device image");fflush(stdout);verify_payload(fd,h,buf,0);
    close(fd);free(buf);puts("IPADOS_ROOT_FULL_SHA256_VERIFIED " IMAGE_SHA);return 0;
}
