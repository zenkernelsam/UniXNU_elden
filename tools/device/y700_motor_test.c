/* SPDX-License-Identifier: MIT
 * Stock Android only. Send bounded constant or stock CLICK through one evdev.
 * No direct registers, calibration, firmware, boost or resonance writes.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <linux/input.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>
static volatile sig_atomic_t cancelled;
static void interrupt(int sig){(void)sig;cancelled=1;}
static void nap(unsigned ms){struct timespec t={ms/1000,(long)(ms%1000)*1000000};while(nanosleep(&t,&t)&&errno==EINTR){} }
static int read_field(char side,const char *field,char *out,size_t size){
 char path[256];snprintf(path,sizeof(path),"/sys/class/leds/vibrator_%c/%s",side,field);
 int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return -1;ssize_t n=read(fd,out,size-1);close(fd);
 if(n<=0)return -1;out[n]=0;return 0;
}
static int number(char side,const char *field){char b[128];if(read_field(side,field,b,sizeof(b)))return -1;char *p=strchr(b,'=');if(!p)return -1;return (int)strtol(p+1,NULL,0);}
static int set_field(char side,const char *field,int value){
 char path[256],b[32];snprintf(path,sizeof(path),"/sys/class/leds/vibrator_%c/%s",side,field);
 int n=snprintf(b,sizeof(b),"%d\n",value),fd=open(path,O_WRONLY|O_CLOEXEC);if(fd<0)return -1;
 int ok=write(fd,b,n)==n;close(fd);return ok?0:-1;
}
static int motor(char side,char *event,size_t size){
 char path[256],actual[PATH_MAX],expected[PATH_MAX],name[128],ic[128];glob_t paths={0};
 snprintf(path,sizeof(path),"/sys/class/leds/vibrator_%c/device",side);
 if(!realpath(path,expected)||!strstr(expected,side=='l'?"/2-005a":"/2-005b"))return -1;
 if(read_field(side,"awinic_ic",ic,sizeof(ic))||!strstr(ic,"AW86937"))return -1;
 if(glob("/sys/class/input/event*",0,NULL,&paths))return -1;
 int result=-1;
 for(size_t i=0;i<paths.gl_pathc;i++){
  if(!realpath(paths.gl_pathv[i],actual)||strncmp(actual,expected,strlen(expected))||actual[strlen(expected)]!='/')continue;
  const char *base=strrchr(paths.gl_pathv[i],'/');snprintf(event,size,"/dev/input/%s",base+1);
  int fd=open(event,O_RDWR|O_CLOEXEC);if(fd<0)break;
  unsigned long bits[(FF_MAX+sizeof(long)*8)/(sizeof(long)*8)]={0};
  if(ioctl(fd,EVIOCGNAME(sizeof(name)),name)<0||strcmp(name,"aw-haptic-hv")||ioctl(fd,EVIOCGBIT(EV_FF,sizeof(bits)),bits)<0||!(bits[FF_CONSTANT/(sizeof(long)*8)]&(1UL<<(FF_CONSTANT%(sizeof(long)*8))))){close(fd);break;}
  result=fd;break;
 }
 globfree(&paths);return result;
}
static int event_write(int fd,unsigned short code,int value){struct input_event ev={0};ev.type=EV_FF;ev.code=code;ev.value=value;return write(fd,&ev,sizeof(ev))==sizeof(ev)?0:-1;}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IOLBF,0);
 if(argc<2||argc>4||(strcmp(argv[1],"survey")&&strcmp(argv[1],"stop")&&strcmp(argv[1],"l")&&strcmp(argv[1],"r"))||
    (argc>=3&&strcmp(argv[2],"gentle")&&strcmp(argv[2],"medium")&&strcmp(argv[2],"crisp"))||
    (argc==4&&strcmp(argv[3],"1")&&strcmp(argv[3],"3"))){fprintf(stderr,"usage: y700_motor_test survey|stop|l|r [gentle|medium|crisp] [1|3]\n");return 2;}
 if(!strcmp(argv[1],"stop")){
  int f=open("/data/local/tmp/y700-motor-test.lock",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);char text[32]={0},path[64],actual[PATH_MAX],self[PATH_MAX];
  if(f<0)return 0;ssize_t n=read(f,text,sizeof(text)-1);close(f);if(n<=0)return 0;
  char *end=NULL;long pid=strtol(text,&end,10);if(pid<=1||!end||(*end!='\n'&&*end!=0))return 1;
  snprintf(path,sizeof(path),"/proc/%ld/exe",pid);
  if(!realpath(path,actual))return errno==ENOENT?0:1;
  if(!realpath("/proc/self/exe",self)||strcmp(actual,self))return 1;
  if(kill((pid_t)pid,SIGTERM)<0&&errno!=ESRCH)return 1;
  puts("STOP requested for verified motor-test process");return 0;
 }
 int crisp=argc>=3&&!strcmp(argv[2],"crisp");
 unsigned test_gain=crisp||(argc>=3&&!strcmp(argv[2],"medium"))?64:32;
 unsigned duration=crisp?100:argc>=3&&!strcmp(argv[2],"medium")?350:250;
 unsigned pulse_count=argc==4&&!strcmp(argv[3],"1")?1:3;
 char paths[2][128];int fd[2]={-1,-1},effect=-1,ret=1,lock=-1;int gain[2],index[2],mode[2],vmax[2];int selected=argv[1][0]=='r';
 lock=open("/data/local/tmp/y700-motor-test.lock",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
 if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){fprintf(stderr,"Another motor test is active\n");goto done;}
 if(ftruncate(lock,0)||dprintf(lock,"%ld\n",(long)getpid())<0){perror("record test owner");goto done;}
 for(int i=0;i<2;i++){
  char side=i?'r':'l';fd[i]=motor(side,paths[i],sizeof(paths[i]));
  gain[i]=number(side,"gain");index[i]=number(side,"index");mode[i]=number(side,"activate_mode");vmax[i]=number(side,"vmax");
  if(fd[i]<0||gain[i]<0||gain[i]>128||index[i]<1||index[i]>5||(mode[i]!=1&&mode[i]!=5)||vmax[i]!=8500){fprintf(stderr,"Unexpected stock motor identity/configuration: %c\n",side);goto done;}
  printf("MOTOR %c %s gain=%d index=%d mode=%d vmax=%d\n",side,paths[i],gain[i],index[i],mode[i],vmax[i]);
 }
 if(!strcmp(argv[1],"survey")){ret=0;goto done;}
 /* RAM one-shots leave the software state flag set after the waveform ends.
  * The desktop also checks the framework is IDLE before this bounded test. */
 if(number('l',"duration")!=0||number('r',"duration")!=0){fprintf(stderr,"Motor busy; wait without system haptics\n");goto done;}
 signal(SIGINT,interrupt);signal(SIGTERM,interrupt);signal(SIGHUP,interrupt);signal(SIGALRM,interrupt);alarm(8);
 char side=selected?'r':'l',other=selected?'l':'r';
 if(crisp){
  unsigned long bits[(FF_MAX+sizeof(long)*8)/(sizeof(long)*8)]={0};
  if(ioctl(fd[selected],EVIOCGBIT(EV_FF,sizeof(bits)),bits)<0||
     !(bits[FF_PERIODIC/(sizeof(long)*8)]&(1UL<<(FF_PERIODIC%(sizeof(long)*8))))||
     !(bits[FF_CUSTOM/(sizeof(long)*8)]&(1UL<<(FF_CUSTOM%(sizeof(long)*8))))||number(side,"ram_num")!=5){
   fprintf(stderr,"Stock click waveform interface unavailable\n");goto done;
  }
 }
 if(test_gain>(unsigned)gain[selected]){fprintf(stderr,"Test gain would exceed current factory gain\n");goto done;}
 if(set_field(side,"gain",(int)test_gain)){perror("set bounded gain");goto restore;}
 struct ff_effect ff={0};ff.type=FF_CONSTANT;ff.id=-1;ff.replay.length=duration;ff.u.constant.level=8192;
 /* Exact stock VibratorOL InputFFDevice::play: Effect::CLICK(0) maps to
  * waveform1, FF_PERIODIC/FF_CUSTOM, three s16 values and custom_len6.
  * This vendor interface selects built-in RAM data; no waveform is uploaded.
  * Driver id1 selects RAM one-shot; magnitude metadata does not raise gain. */
 __s16 click_data[3]={1,16383,0};
 if(crisp){
  memset(&ff,0,sizeof(ff));ff.type=FF_PERIODIC;ff.id=-1;ff.replay.length=duration;
  ff.u.periodic.waveform=FF_CUSTOM;ff.u.periodic.magnitude=16383;
  ff.u.periodic.custom_len=6;ff.u.periodic.custom_data=click_data;
 }
 if(ioctl(fd[selected],EVIOCSFF,&ff)<0){perror("upload effect");goto restore;}
 effect=ff.id;
 for(unsigned pulse=1;pulse<=pulse_count&&!cancelled;pulse++){
  if(number(other,"duration")!=0){fprintf(stderr,"Other motor active; cancelling test\n");goto restore;}
  if(crisp)printf("PULSE %c %u/%u stock-CLICK RAM1 gain%u guard=%ums\n",side,pulse,pulse_count,test_gain,duration);
  else printf("PULSE %c %u/%u %ums gain%u\n",side,pulse,pulse_count,duration,test_gain);
  if(event_write(fd[selected],effect,1)){perror("play");goto restore;}
  nap(75);
  if(crisp)printf("CLICK selected-mode=%d index=%d gain=%d; RAM one-shot uses waveform end, not duration timer; other-timer=%dms\n",number(side,"activate_mode"),number(side,"index"),number(side,"gain"),number(other,"duration"));
  else printf("TIMER selected=%dms other=%dms\n",number(side,"duration"),number(other,"duration"));
  for(unsigned elapsed=75;elapsed<duration+40&&!cancelled;elapsed+=20)nap(20);
  if(event_write(fd[selected],effect,0)){perror("stop");goto restore;}
  for(unsigned elapsed=0;elapsed<700&&!cancelled;elapsed+=20)nap(20);
 }
 ret=cancelled?130:0;
restore:
 if(effect>=0){if(event_write(fd[selected],effect,0))ret=1;nap(80);if(ioctl(fd[selected],EVIOCRMFF,effect))ret=1;}
 if(set_field(side,"activate",0))ret=1;
 nap(100);
 if(set_field(side,"gain",gain[selected]))ret=1;
 if(set_field(side,"index",index[selected]))ret=1;
 if(set_field(side,"activate_mode",mode[selected]))ret=1;
 nap(60);alarm(0);
 if(number(side,"gain")!=gain[selected]||number(side,"index")!=index[selected]||number(side,"activate_mode")!=mode[selected]||number(side,"vmax")!=vmax[selected]||number(side,"duration")!=0)ret=1;
 printf("RESTORED %c gain=%d index=%d mode=%d vmax=%d timer=%d result=%d\n",side,number(side,"gain"),number(side,"index"),number(side,"activate_mode"),number(side,"vmax"),number(side,"duration"),ret);
done:
 for(int i=0;i<2;i++)if(fd[i]>=0)close(fd[i]);if(lock>=0)close(lock);return ret;
}
