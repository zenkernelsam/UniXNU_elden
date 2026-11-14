"""Compile the actual helper predicate with narrow kernel-state test doubles."""
from pathlib import Path
import argparse,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    prefix=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#define OS_REASON_CODESIGNING 3
#define CODESIGNING_EXIT_REASON_LAUNCH_CONSTRAINT_VIOLATION 16
#define CS_VALID 1
#define CS_SIGNED 0x20000
#define MNT_RDONLY 1
#define MNT_UNION 32
#define VREG 1
#define MAXPATHLEN 1024
typedef struct proc{const char *name;} *proc_t;
typedef struct reason{int osr_namespace,osr_code;char *text;} *os_reason_t;
struct _posix_spawnattr{int psa_launch_type;};
struct mount{char f_fstypename[16];};typedef struct mount *mount_t;
struct image_params{int ip_mac_return;void *ip_vp,*ip_px_sa;unsigned ip_csflags;};
static struct mount m={"bindfs"};static int ready=1,mode=4,type=VREG,flags=MNT_RDONLY|MNT_UNION,path_error,hash_error,hash_bad,mount_null;
static const char *path_value="/usr/sbin/BlueTool";
const char *proc_best_name(proc_t p){return p->name;}int proc_pid(proc_t p){(void)p;return 79;}
int elden_bt_runtime_ready(void){return ready;}
int PE_parse_boot_argn(const char *n,void *out,int bytes){(void)n;(void)bytes;*(int*)out=mode;return 1;}
mount_t vnode_mount(void *p){(void)p;return mount_null?0:&m;}
int vnode_vtype(void *p){(void)p;return type;}
mount_t vfs_statfs(mount_t p){return p;}int vfs_flags(mount_t p){(void)p;return flags;}
int vn_getpath(void *p,char *out,int *length){(void)p;(void)length;strcpy(out,path_value);return path_error;}
char *exit_reason_get_string_desc(os_reason_t r){return r->text;}
char *strnstr(const char *p,const char *q,size_t n){(void)n;return strstr(p,q);}
int proc_getcdhash(proc_t p,unsigned char *out){(void)p;const unsigned char h[20]={0xe8,0x8b,0x9a,0xee,0x56,0x8d,0x04,0x3b,0xaf,0x7a,0x20,0xca,0xf4,0x69,0x6c,0x05,0x1b,0x2c,0xa8,0x02};memcpy(out,h,20);out[19]^=hash_bad;return hash_error;}
'''
    suffix=r'''
int main(void){
struct proc process={"BlueTool"};struct reason reason={3,16,"c[8]p[1]m[1]e[6], (Constraint not matched)"};
struct _posix_spawnattr attr={1};struct image_params image={0,&m,&attr,CS_VALID|CS_SIGNED};
#define CHECK() elden_bt_owner_launch(&process,&image,EPERM,&reason)
#define REJECT(change,restore) do{change;assert(!CHECK());restore;}while(0)
assert(CHECK());assert(!elden_bt_owner_launch(0,&image,EPERM,&reason));assert(!elden_bt_owner_launch(&process,0,EPERM,&reason));
assert(!elden_bt_owner_launch(&process,&image,EACCES,&reason));assert(!elden_bt_owner_launch(&process,&image,EPERM,0));
REJECT(process.name="bluetoothd",process.name="BlueTool");REJECT(process.name="BlueToolX",process.name="BlueTool");
REJECT(reason.osr_namespace=4,reason.osr_namespace=3);REJECT(reason.osr_code=15,reason.osr_code=16);
REJECT(image.ip_mac_return=EPERM,image.ip_mac_return=0);REJECT(image.ip_vp=0,image.ip_vp=&m);REJECT(image.ip_px_sa=0,image.ip_px_sa=&attr);
REJECT(attr.psa_launch_type=0,attr.psa_launch_type=1);REJECT(image.ip_csflags=CS_VALID,image.ip_csflags=CS_VALID|CS_SIGNED);REJECT(image.ip_csflags=CS_SIGNED,image.ip_csflags=CS_VALID|CS_SIGNED);
REJECT(ready=0,ready=1);REJECT(mode=3,mode=4);REJECT(type=2,type=VREG);REJECT(flags=MNT_RDONLY,flags=MNT_RDONLY|MNT_UNION);REJECT(flags=MNT_UNION,flags=MNT_RDONLY|MNT_UNION);
REJECT(mount_null=1,mount_null=0);REJECT(strcpy(m.f_fstypename,"apfs"),strcpy(m.f_fstypename,"bindfs"));
REJECT(path_error=5,path_error=0);REJECT(path_value="/tmp/BlueTool",path_value="/usr/sbin/BlueTool");
char *normal=reason.text;REJECT(reason.text=0,reason.text=normal);REJECT(reason.text="c[8]p[1]m[5]e[5], (Constraint not matched)",reason.text=normal);
char big[1025];memset(big,'a',1024);big[1024]=0;REJECT(reason.text=big,reason.text=normal);
REJECT(hash_error=1,hash_error=0);REJECT(hash_bad=1,hash_bad=0);assert(CHECK());puts("PASS all helper qualification failures and accepted exact fixture");}
'''
    code=out/'test.c';code.write_text(prefix+(ROOT/'drivers/EldenBluetooth/runtime/OwnerLaunch.inc').read_text()+suffix)
    cmd=['cc','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(code),'-o',str(out/'test')]
    for name,command in [('compile',cmd),('run',[str(out/'test')])]:
        r=subprocess.run(command,capture_output=True);(out/(name+'.log')).write_bytes(r.stdout+r.stderr);r.check_returncode()
    (out/'result.json').write_text(json.dumps({'passed':True,'actual_function':True,'sanitizers':['ASan','UBSan'],'hardware':False,'command':cmd},indent=2)+'\n');print('PASS actual helper launch predicate')
if __name__=='__main__':main()
