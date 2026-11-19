"""Allow only actual source-qualified explicit sensor binding call edges.

This is a call-chain audit, never a blanket allow for any internal call or
mere lack of mod_init. Consumer start entrypoints must have no incoming caller
until mainline installs a real provider. Every allowed call stays inside the
actual named compiled function and its adjacent exact boundary.
"""
import re,subprocess
from pathlib import Path
ALLOWED_RPC={
 '_elden_sensor_rpc_open':[('_elden_sensor_pack_binding_open','_elden_sensor_pack_binding_channel')],
 '_elden_sensor_rpc_channel':[('_elden_sensor_pack_binding_channel','_elden_sensor_pack_binding_receive')],
 '_elden_sensor_rpc_receive':[('_elden_sensor_pack_binding_receive','_elden_sensor_pack_binding_step')],
 '_elden_sensor_rpc_step':[('_elden_sensor_pack_binding_step','_elden_sensor_pack_binding_request_stop')],
 '_elden_sensor_rpc_request_stop':[('_elden_sensor_pack_binding_request_stop','_elden_sensor_pack_binding_finish'),
                                   ('_elden_sensor_pack_binding_finish','_elden_sensor_pack_binding_status'),
                                   ('_elden_sensor_rpc_finish','_elden_sensor_rpc_status')],
 '_elden_sensor_rpc_finish':[('_elden_sensor_pack_binding_finish','_elden_sensor_pack_binding_status')],
 '_elden_sensor_rpc_status':[('_elden_sensor_pack_binding_status','_elden_sensor_memory_native_open')],
}
def explicit_binding_calls(name,calls,symbol,native_path=None):
    target=symbol(name);hits=[pc for pc,t in calls if t==target]
    if not hits:return []
    if name=='_elden_sensor_pack_binding_open':bounds=[('_elden_sensor_memory_binding_open','_elden_sensor_memory_binding_channel')]
    else:bounds=ALLOWED_RPC.get(name)
    if bounds is None:raise ValueError('No qualified binding caller for '+name)
    ranges=[]
    actual_symbols=[]
    if native_path is not None:
        for line in subprocess.check_output(['xcrun','nm','-n',str(Path(native_path))],text=True).splitlines():
            m=re.match(r'^([0-9a-fA-F]+)\s+[sStT]\s+(\S+)$',line)
            if m:actual_symbols.append((int(m[1],16),m[2]))
    for start_name,end_name in bounds:
        start,end=map(symbol,(start_name,end_name))
        if not start<end:raise ValueError('Exact binding function order changed: '+start_name)
        if actual_symbols:
            boundary=min((pc for pc,_n in actual_symbols if pc>start),default=None)
            if boundary is None:raise ValueError('Compiled next-function boundary absent: '+start_name)
            # Exact adjacent public boundary normally matches. For a last
            # public wrapper before another object, the symbol table gives
            # its actual end, never the next far-away public function.
            end=min(end,boundary)
        ranges.append((start,end,start_name))
    result=[]
    for pc in hits:
        matched=[(start,end,n) for start,end,n in ranges if start<=pc<end]
        if len(matched)!=1:raise ValueError('Unqualified incoming call outside exact binding function: '+name)
        start,end,n=matched[0];result.append({'pc':hex(pc),'explicit_source_function':n,'function_start':hex(start),'next_function':hex(end)})
    return result
def no_outer_sensor_start(calls,symbol):
    for name in ('_elden_sensor_memory_binding_open','_elden_sensor_adsp_endpoint_open'):
        target=symbol(name)
        if any(t==target for pc,t in calls):raise ValueError('Outer actual sensor startup has an unqualified caller: '+name)
