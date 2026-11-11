/* Fixed reviewed GPT update. Only four 4096-byte GPT sectors can be written. */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <mntent.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <openssl/sha.h>
#include "gpt_hashes.h"
#define DISK_BYTES (248838144ULL*4096)
#define BACKUP_START (248838139ULL*4096)
static uint64_t u64(const void *p){uint64_t v;memcpy(&v,p,8);return v;}
static int get(int fd,void *buf,size_t n,uint64_t off){
    unsigned char *p=buf;while(n){ssize_t r=pread(fd,p,n,(off_t)off);if(r<0&&errno==EINTR)continue;if(r<=0)return -1;p+=r;n-=r;off+=r;}return 0;
}
static int put(int fd,const void *buf,uint64_t off){
    const unsigned char *p=buf;size_t n=4096;while(n){ssize_t r=pwrite(fd,p,n,(off_t)off);if(r<0&&errno==EINTR)continue;if(r<=0)return -1;p+=r;n-=r;off+=r;}return fsync(fd);
}
static int hash_ok(const void *p,size_t n,const char *expected){
    unsigned char d[32];char hex[65];SHA256(p,n,d);for(unsigned i=0;i<32;i++)sprintf(hex+2*i,"%02x",d[i]);return !strcmp(hex,expected);
}
static int file(const char *path,void *p,size_t n){
    int fd=open(path,O_RDONLY|O_CLOEXEC);struct stat st;
    if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||(uint64_t)st.st_size!=n)return -1;
    int r=get(fd,p,n,0);close(fd);return r;
}
int main(int argc,char **argv){
    if(argc!=2||strcmp(argv[1],"--apply-reviewed-lun0-128g"))return 2;
    unsigned char main_old[24576],backup_old[20480],main_new[24576],backup_new[20480],sb[8192];
    if(file("/tmp/elden-gpt-post-main.bin",main_new,sizeof(main_new))||file("/tmp/elden-gpt-post-backup.bin",backup_new,sizeof(backup_new)))return 3;
    if(!hash_ok(main_new,sizeof(main_new),POST_MAIN_SHA256)||!hash_ok(backup_new,sizeof(backup_new),POST_BACKUP_SHA256))return 4;
    if(u64(main_new+4096+72)!=2||u64(backup_new+16384+72)!=248838139ULL)return 5;
    int map=open("/dev/block/mapper/userdata",O_RDONLY|O_CLOEXEC);struct stat ms;
    if(map<0||fstat(map,&ms)||!S_ISBLK(ms.st_mode)||get(map,sb,sizeof(sb),0))return 6;
    close(map);
    if(memcmp(sb+1024,sb+5120,3072)||u64(sb+1060)!=209310803ULL)return 7;
    FILE *mounts=setmntent("/proc/mounts","r");if(!mounts)return 8;struct mntent *entry;
    while((entry=getmntent(mounts))){struct stat s;if(!stat(entry->mnt_fsname,&s)&&S_ISBLK(s.st_mode)&&s.st_rdev==ms.st_rdev){endmntent(mounts);return 9;}}
    endmntent(mounts);
    int fd=open("/dev/block/sda",O_RDWR|O_SYNC|O_CLOEXEC);struct stat ds;uint64_t bytes;int sector;
    if(fd<0||fstat(fd,&ds)||!S_ISBLK(ds.st_mode)||ioctl(fd,BLKGETSIZE64,&bytes)||bytes!=DISK_BYTES||ioctl(fd,BLKSSZGET,&sector)||sector!=4096)return 10;
    if(get(fd,main_old,sizeof(main_old),0)||get(fd,backup_old,sizeof(backup_old),BACKUP_START))return 11;
    if(!hash_ok(main_old,sizeof(main_old),PRE_MAIN_SHA256)||!hash_ok(backup_old,sizeof(backup_old),PRE_BACKUP_SHA256))return 12;
    puts("Preimages, shrunk filesystem and unmounted state verified.");fflush(stdout);
    if(put(fd,backup_new,BACKUP_START))return 20;
    if(put(fd,backup_new+16384,248838143ULL*4096))return 21;
    puts("Backup GPT committed.");fflush(stdout);
    if(put(fd,main_new+8192,8192))return 22;
    if(put(fd,main_new+4096,4096))return 23;
    if(get(fd,main_old,sizeof(main_old),0)||get(fd,backup_old,sizeof(backup_old),BACKUP_START))return 24;
    if(!hash_ok(main_old,sizeof(main_old),POST_MAIN_SHA256)||!hash_ok(backup_old,sizeof(backup_old),POST_BACKUP_SHA256))return 25;
    close(fd);puts("GPT128 VERIFIED. Reboot before using new partition nodes.");return 0;
}
