"""Check the actual integer tilt code over every supported degree pair."""
from pathlib import Path
import argparse,hashlib,json,math,subprocess
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenPen'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);exe=out/'tilt-test';harness=out/'Test.c'
    harness.write_text('''#include "Tilt.h"
#include <stdio.h>
#include <assert.h>
int main(void){
 for(int x=-60;x<=60;++x)for(int y=-60;y<=60;++y){EldenPenTilt a;assert(!elden_pen_tilt(x,y,&a));printf("%d,%d,%d,%d\\n",x,y,a.altitude_degrees,a.azimuth_degrees);}
 const int invalid[][2]={{-32768,0},{32767,0},{0,-61},{61,60}};
 for(unsigned i=0;i<4;++i){EldenPenTilt a={123,456};assert(elden_pen_tilt(invalid[i][0],invalid[i][1],&a)==-1);assert(a.altitude_degrees==123&&a.azimuth_degrees==456);}
 assert(elden_pen_tilt(0,0,0)==-1);return 0;
}
''')
    cmd=['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(DRIVER),str(DRIVER/'Tilt.c'),str(harness),'-o',str(exe)]
    r=subprocess.run(cmd,capture_output=True);(out/'compile.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
    r=subprocess.run([str(exe)],capture_output=True);(out/'run.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
    rows=[list(map(int,line.split(','))) for line in r.stdout.decode().splitlines()];assert len(rows)==14641
    observed={(x,y):(alt,az) for x,y,alt,az in rows};assert len(observed)==14641
    count=0;max_alt=max_az=0
    for x in range(-60,61):
        for y in range(-60,61):
            altitude,azimuth=observed[x,y]
            tx=math.tan(math.radians(x));ty=math.tan(math.radians(y))
            expected_alt=math.degrees(math.atan2(1,math.hypot(tx,ty)))
            expected_az=(math.degrees(math.atan2(ty,tx))%360) if x or y else 0
            alt_error=abs(altitude/65536-expected_alt)
            az_error=abs((azimuth/65536-expected_az+180)%360-180)
            assert alt_error<=16/65536 and az_error<=16/65536,(x,y,altitude,azimuth,alt_error,az_error)
            assert 0<=altitude<=90*65536 and 0<=azimuth<360*65536
            max_alt=max(max_alt,alt_error);max_az=max(max_az,az_error);count+=1
    report={'passed':True,'degree_pairs':count,'max_altitude_error_degrees':max_alt,'max_azimuth_error_degrees':max_az,
        'sources':{n:hashlib.sha256((DRIVER/n).read_bytes()).hexdigest() for n in ['Tilt.c','Tilt.h','TiltConstants.h']},
        'oracle':'independent host double tan/hypot/atan2','asan_ubsan':True,'hardware_mounting_verified':False,'UIKit_units_verified':False}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
