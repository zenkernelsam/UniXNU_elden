"""Generate the small mathematical constants, not device calibration data."""
from pathlib import Path
import argparse,math
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists():raise ValueError('New output required')
    tangent=[round(math.tan(math.radians(i))*(1<<24)) for i in range(61)]
    arctangent=[round(math.degrees(math.atan(2.0**-i))*65536) for i in range(24)]
    text='/* SPDX-License-Identifier: BSD-2-Clause-Patent */\n/* Mathematical Q24 tan and Q16 degree atan constants; no board calibration. */\n#pragma once\n#include <stdint.h>\n'
    for name,values in [('tan',tangent),('atan',arctangent)]:
        text+='static const int32_t elden_pen_'+name+'[] = {\n'
        text+='\n'.join('    '+','.join(str(x) for x in values[i:i+8])+',' for i in range(0,len(values),8))+'\n};\n'
    a.output.write_text(text)
if __name__=='__main__':main()
