/* One reviewed F2FS shrink on this tablet; never edits GPT or encryption keys.
 * Distinct from the read-only availability probe. No arbitrary size argument. */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

static uint32_t u32(const unsigned char *p){uint32_t x;memcpy(&x,p,4);return x;}
static uint64_t u64(const unsigned char *p){uint64_t x;memcpy(&x,p,8);return x;}
static int read_sb(int fd,unsigned char *b){return pread(fd,b,8192,0)==8192 && !memcmp(b+1024,b+5120,3072);}
int main(int argc,char **argv){
    if(argc!=2 || strcmp(argv[1],"--apply-reviewed-128g")){puts("Requires --apply-reviewed-128g; no operation performed");return 2;}
    struct utsname uts;struct stat mapstat,mountstat;struct statfs fs;struct statvfs vfs;
    if(geteuid()!=0 || uname(&uts) || strcmp(uts.release,"6.6.118-android15-8-g350f378eb496-ab14943902-4k"))return 3;
    int map=open("/dev/block/mapper/userdata",O_RDONLY|O_CLOEXEC);uint64_t size=0;int block=0;
    if(map<0 || fstat(map,&mapstat) || !S_ISBLK(mapstat.st_mode) || ioctl(map,BLKGETSIZE64,&size) ||
       size!=994778099712ULL || ioctl(map,BLKSSZGET,&block) || block!=4096)return 4;
    if(stat("/data",&mountstat) || mountstat.st_dev!=mapstat.st_rdev || statfs("/data",&fs) ||
       fs.f_type!=0xf2f52010 || fs.f_bsize!=4096 || statvfs("/data",&vfs) || (vfs.f_flag&ST_RDONLY))return 5;
    unsigned char before[8192],after[8192];
    if(!read_sb(map,before))return 6;
    const unsigned char *s=before+1024;
    if(u32(s)!=0xf2f52010 || u32(s+16)!=12 || u32(s+20)!=9 || u32(s+24)!=1 ||
       u64(s+36)!=242865747ULL || u32(s+44)!=473297 || u32(s+68)!=473297 ||
       u32(s+92)!=537600 || u32(s+2180)!=0x1499)return 7;
    if(fs.f_bavail<33554944ULL+5120)return 8;
    int fd=open("/data",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(fd<0)return 9;
    uint64_t requested=209310720ULL;
    printf("APPLY old=242865747 request=209310720 expected_final=209310803 future_capacity=209311064\n");fflush(stdout);
    int ret=ioctl(fd,0x4008f510UL,&requested),saved=errno;
    if(ret){printf("FAILED ret=%d errno=%d; do not change GPT\n",ret,saved);close(fd);close(map);return 10;}
    if(syncfs(fd)){puts("FAILED syncfs; do not change GPT");return 11;}
    close(fd);
    if(!read_sb(map,after)){puts("FAILED post-superblock agreement; do not change GPT");return 12;}
    close(map);s=after+1024;
    printf("RESULT blocks=%llu sections=%u main_segments=%u\n",(unsigned long long)u64(s+36),u32(s+44),u32(s+68));
    if(u64(s+36)!=209310803ULL || u32(s+44)!=407760 || u32(s+68)!=407760 || u32(s+92)!=537600){
        puts("FAILED unexpected geometry; do not change GPT");return 13;
    }
    puts("F2FS shrink returned success and both superblocks match; GPT unchanged. Further checkpoint verification required.");return 0;
}
