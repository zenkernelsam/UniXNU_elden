/* Read-only raw userdata extent streamer. No device write operation exists. */
#define _FILE_OFFSET_BITS 64
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <openssl/sha.h>
#include <errno.h>

struct header { char magic[8]; uint64_t version, logical, total, count; };
struct extent { uint64_t offset, bytes; };
static int exact(int fd, void *data, size_t size) {
    unsigned char *p=data;
    while(size) { ssize_t n=read(fd,p,size); if(n<0 && errno==EINTR)continue;
        if(n<=0)return -1;p+=n;size-=(size_t)n; }
    return 0;
}
static int output(const void *data,size_t size) {
    const unsigned char *p=data;
    while(size) { ssize_t n=write(STDOUT_FILENO,p,size); if(n<0 && errno==EINTR)continue;
        if(n<=0)return -1;p+=n;size-=(size_t)n; }
    return 0;
}
int main(int argc,char **argv) {
    int hash_only=argc==4 && !strcmp(argv[3],"--hash");
    if((argc!=3 && !hash_only) || strcmp(argv[1],"/dev/block/by-name/userdata"))return 2;
    int plan=open(argv[2],O_RDONLY|O_CLOEXEC);struct header h;struct stat st;
    if(plan<0 || fstat(plan,&st) || !S_ISREG(st.st_mode) || exact(plan,&h,sizeof(h)))return 3;
    if(memcmp(h.magic,"ELDEXT1\0",8) || h.version!=1 || h.logical!=994778099712ULL ||
       !h.count || h.count>1000000 || !h.total || h.total>h.logical ||
       (uint64_t)st.st_size!=sizeof(h)+h.count*sizeof(struct extent))return 4;
    struct extent *ranges=malloc(h.count*sizeof(*ranges));
    if(!ranges || exact(plan,ranges,h.count*sizeof(*ranges)))return 5;
    close(plan);uint64_t end=0,sum=0;
    for(uint64_t i=0;i<h.count;i++) {
        struct extent e=ranges[i];
        if(!e.bytes || e.offset<end || e.offset>h.logical || e.bytes>h.logical-e.offset ||
           (e.offset&4095) || (e.bytes&4095) || e.bytes>h.total-sum)return 6;
        end=e.offset+e.bytes;sum+=e.bytes;
    }
    if(sum!=h.total || ranges[0].offset!=0 || ranges[0].bytes<537600ULL*4096)return 7;
    int fd=open(argv[1],O_RDONLY|O_CLOEXEC);uint64_t bytes=0;int blocksize=0;
    if(fd<0 || fstat(fd,&st) || !S_ISBLK(st.st_mode) || ioctl(fd,BLKGETSIZE64,&bytes) ||
       ioctl(fd,BLKSSZGET,&blocksize) || bytes!=h.logical || blocksize!=4096)return 8;
    unsigned char *buffer=malloc(1048576);if(!buffer)return 9;
    SHA256_CTX sha;SHA256_Init(&sha);uint64_t completed=0,next=268435456;
    for(uint64_t i=0;i<h.count;i++) {
        if(lseek(fd,(off_t)ranges[i].offset,SEEK_SET)!=(off_t)ranges[i].offset)return 10;
        uint64_t left=ranges[i].bytes;
        while(left) {
            size_t n=left>1048576?1048576:(size_t)left;
            if(exact(fd,buffer,n))return 11;
            SHA256_Update(&sha,buffer,n);
            if(!hash_only && output(buffer,n))return 12;
            left-=n;completed+=n;
            if(completed>=next) {dprintf(2,"read %llu / %llu bytes\n",(unsigned long long)completed,(unsigned long long)h.total);next=completed+268435456;}
        }
    }
    unsigned char digest[32];SHA256_Final(digest,&sha);close(fd);
    if(hash_only) {printf("OK bytes=%llu sha256=",(unsigned long long)completed);for(unsigned i=0;i<32;i++)printf("%02x",digest[i]);puts("");}
    free(buffer);free(ranges);return 0;
}
