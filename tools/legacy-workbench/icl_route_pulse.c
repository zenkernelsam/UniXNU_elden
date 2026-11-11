/* SPDX-License-Identifier: MIT */
/* TB322FC stock Android: standard USB ICL=0 suspend for five seconds,
 * then restore the observed 500mA budget through the same sysfs API.
 * This issues charger RPCs; no GPIO, PD-voltage, role or disk changes. */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <signal.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ms(void) {
    struct timespec t; if (clock_gettime(CLOCK_MONOTONIC, &t)) _exit(100);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
static void until_ms(uint64_t end) {
    struct timespec t = {.tv_sec=(time_t)(end/1000), .tv_nsec=(long)(end%1000)*1000000};
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL)==EINTR) {}
}
static int text_read(const char *path, char *out, size_t cap) {
    int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return -1;
    ssize_t n=read(fd,out,cap-1);int saved=errno;close(fd);errno=saved;
    if(n<=0)return -1;
    out[n]=0;return 0;
}
static int number(const char *path, long *value) {
    char buf[128],*end;if(text_read(path,buf,sizeof(buf)))return -1;
    errno=0;long n=strtol(buf,&end,10);if(errno||end==buf)return -1;
    while(isspace((unsigned char)*end))++end;
    if(*end)return -1;
    *value=n;return 0;
}
static int temp_path(const char *name,char *out,size_t cap) {
    unsigned found=0;
    for(unsigned i=0;i<128;++i) {
        char path[128],buf[96];snprintf(path,sizeof(path),"/sys/class/thermal/thermal_zone%u/type",i);
        if(text_read(path,buf,sizeof(buf)))continue;
        buf[strcspn(buf,"\r\n")]=0;
        if(!strcmp(buf,name)){snprintf(out,cap,"/sys/class/thermal/thermal_zone%u/temp",i);++found;}
    }
    return found==1?0:-1;
}
static int value_get(int unused) {
    (void)unused;long value;
    if(number("/sys/class/power_supply/usb/input_current_limit",&value)||value<0||value>3000000)return -1;
    return (int)value;
}
static int value_set(int unused,int value) {
    (void)unused;if(value!=0&&value!=500000){errno=EINVAL;return -1;}
    int fd=open("/sys/class/power_supply/usb/input_current_limit",O_WRONLY|O_CLOEXEC);if(fd<0)return -1;
    char text[32];int length=snprintf(text,sizeof(text),"%d\n",value);ssize_t wrote=write(fd,text,(size_t)length);
    int saved=errno;close(fd);errno=saved;return wrote==length?0:-1;
}
static int cool(const char *path) {
    long temp;return !number(path,&temp)&&temp>=10000&&temp<40000;
}
static int admit(const char *one,const char *two) {
    long soc,voltage,temp;
    return !number("/sys/class/power_supply/battery/capacity",&soc)&&soc>=40&&soc<=100&&
        !number("/sys/class/power_supply/battery/voltage_now",&voltage)&&voltage>=3800000&&voltage<=4500000&&
        !number("/sys/class/power_supply/battery/temp",&temp)&&temp>=100&&temp<400&&cool(one)&&cool(two);
}
static void sample(int fd,const char *phase) {
    static const char *const paths[]={
        "/sys/class/power_supply/battery/current_now",
        "/sys/class/power_supply/usb/online",
        "/sys/class/power_supply/usb/voltage_now",
        "/sys/class/power_supply/usb/current_now",
        "/sys/class/power_supply/wireless/online",
        "/sys/class/power_supply/wireless/voltage_now",
        "/sys/class/power_supply/wireless/current_now",
        "/sys/class/qcom-battery/fg1_current",
        "/sys/class/qcom-battery/fg2_current",
        "/sys/class/qcom-battery/pps_status"};
    printf("sample t=%llu phase=%s icl=%d\n",(unsigned long long)now_ms(),phase,value_get(fd));
    char state[64];if(!text_read("/sys/class/udc/a600000.dwc3/state",state,sizeof(state))){state[strcspn(state,"\r\n")]=0;printf("udc t=%llu state=%s\n",(unsigned long long)now_ms(),state);}
    for(unsigned i=0;i<sizeof(paths)/sizeof(paths[0]);++i){
        long v=0;int error=number(paths[i],&v);
        printf("value t=%llu key=%s error=%d value=%ld\n",(unsigned long long)now_ms(),paths[i],error,v);
    }
}
int main(int argc,char **argv) {
    int pulse=argc==2&&!strcmp(argv[1],"--suspend-5s");
    if(!pulse&&(argc!=2||strcmp(argv[1],"--check")))return 2;
    setvbuf(stdout,NULL,_IONBF,0);
    char t1[128]={0},t2[128]={0};
    if(temp_path("usb1-conn-therm",t1,sizeof(t1))||temp_path("usb2-conn-therm",t2,sizeof(t2))||!admit(t1,t2)){
        puts("refused power/temperature gate");return 3;
    }
    char type[64];long online;
    if(text_read("/sys/class/power_supply/usb/real_type",type,sizeof(type)))return 4;
    type[strcspn(type,"\r\n")]=0;
    if(strcmp(type,"SDP")||number("/sys/class/power_supply/usb/online",&online)||online!=1||value_get(0)!=500000){
        puts("refused source/ICL baseline");return 5;
    }
    int fd=-1,chip=-1; /* shared control flow has no GPIO or chip handle */
    sample(fd,"before");
    if(!pulse){puts("check complete; value writes=0");close(fd);close(chip);return 0;}
    if(!admit(t1,t2)||value_get(fd)!=500000){puts("refused final gate");close(fd);close(chip);return 8;}
    int ready[2],start[2];
    if(pipe(ready)){close(fd);close(chip);return 9;}
    if(pipe(start)){close(ready[0]);close(ready[1]);close(fd);close(chip);return 9;}
    pid_t child=fork();
    if(child<0){close(fd);close(chip);close(ready[0]);close(ready[1]);close(start[0]);close(start[1]);return 10;}
    if(!child){
        close(ready[0]);close(start[1]);close(chip);
        /* Independent of the ADB connection and of the sampling parent's I/O. */
        signal(SIGHUP,SIG_IGN);
        char token='R';if(write(ready[1],&token,1)!=1)_exit(11);close(ready[1]);
        /* Start the deadline at the actual suspend/parent notification,
         * not at fork: a descheduled parent must never assert after expiry.
         * Also observe ICL so parent death between SET and pipe write is safe. */
        int parent_gone=0;
        for(;;){
            int v=value_get(fd);if(v!=500000)break;
            struct pollfd p={.fd=start[0],.events=POLLIN|POLLHUP};
            int n=poll(&p,1,10);if(n<0&&errno==EINTR)continue;
            if(n<0)break;
            if(n>0){char c;ssize_t got=read(start[0],&c,1);parent_gone=got==0;break;}
        }
        close(start[0]);
        if(!parent_gone)until_ms(now_ms()+5000);
        int before=value_get(fd),rc=0;
        if(before<0)rc=12;
        else if(before!=500000){
            /* Keep the USB input suspended if the connector is no longer cool. */
            if(!cool(t1))rc=13;
            else if(value_set(fd,500000)||value_get(fd)!=500000)rc=14;
        }
        printf("guardian t=%llu before=%d restored=%d error=%d\n",(unsigned long long)now_ms(),before,value_get(fd),rc);
        close(fd);_exit(rc);
    }
    close(ready[1]);close(start[0]);signal(SIGPIPE,SIG_IGN);
    char token=0;ssize_t n=read(ready[0],&token,1);close(ready[0]);
    int rc=0;
    if(n!=1||token!='R'){puts("guardian not armed; no ICL write");rc=15;}
    else {
        rc=value_set(fd,0)?16:0;
        char notification='S';if(write(start[1],&notification,1)!=1)rc=19;
        printf("suspend-input t=%llu error=%d level=%d guardian=%ld\n",(unsigned long long)now_ms(),rc,value_get(fd),(long)child);
        for(unsigned i=0;i<20;i++){sample(fd,"observe");until_ms(now_ms()+250);}
    }
    close(start[1]);
    int status=0;if(waitpid(child,&status,0)!=child||!WIFEXITED(status)||WEXITSTATUS(status))rc=17;
    int final=value_get(fd);printf("finished t=%llu icl=%d status=%d\n",(unsigned long long)now_ms(),final,rc);
    close(fd);close(chip);return rc?rc:(final==500000?0:18);
}
