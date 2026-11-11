"""Offline admission/state gate for future unattended boot-debug cycles.

This module deliberately performs no device I/O. A transport executor may act
only on an admitted action; unsupported recovery or unknown device state stops
the cycle. Current CDC candidates advertise fastboot=unqualified.
"""
from dataclasses import dataclass,field
from pathlib import Path
from avb_guard import check_image

@dataclass
class CycleGate:
    maximum_cycles:int=3
    cycles:int=0
    state:str='wait_log'
    candidate_hash:str|None=None
    fastboot_qualified:bool=False
    auto_entry_qualified:bool=False
    repeated_failures:dict=field(default_factory=dict)
    reason:str=''

    def stop(self,reason):
        self.state='operator_required';self.reason=reason;return 'stop'

    def capabilities(self,fastboot=False,auto_entry=False):
        self.fastboot_qualified=fastboot;self.auto_entry_qualified=auto_entry

    def candidate(self,image:Path):
        if self.state not in ('wait_log','analysis'):raise ValueError('candidate at invalid state')
        checked=check_image(image,'boot_a')
        if checked.bytes!=96*1024*1024:raise ValueError('boot_a must be a full 96MiB admitted image')
        self.candidate_hash=checked.sha256;self.state='candidate_ready';return checked

    def begin_recovery(self):
        if self.state!='candidate_ready' or not self.candidate_hash:raise ValueError('no admitted candidate')
        if self.cycles>=self.maximum_cycles:return self.stop('cycle limit')
        if not self.fastboot_qualified:return self.stop('kernel-to-fastboot route not qualified')
        if not self.auto_entry_qualified:return self.stop('menu auto-entry not qualified')
        self.state='wait_fastboot';return 'request_reboot_fastboot'

    def observe_fastboot(self,serial_matches,locked,slot):
        if self.state!='wait_fastboot':raise ValueError('unexpected fastboot observation')
        if not serial_matches or not locked or slot!='a':return self.stop('device/lock/slot mismatch')
        self.state='wait_write_verify';return 'allow_boot_a_write_after_fresh_gpt_and_admission'

    def written(self,digest):
        if self.state!='wait_write_verify':raise ValueError('unexpected write result')
        if digest!=self.candidate_hash:return self.stop('full device digest mismatch')
        self.cycles+=1;self.state='wait_log';return 'allow_reset'

    def stalled(self,stage):return self.stop('no qualified recovery at '+stage)

    def failure(self,fingerprint):
        count=self.repeated_failures.get(fingerprint,0)+1;self.repeated_failures[fingerprint]=count
        if count>=2:return self.stop('same failure repeated twice')
        self.state='analysis';return 'retain_log_and_request_a_new_candidate'

def selftest():
    g=CycleGate();g.state='candidate_ready';g.candidate_hash='a'*64
    assert g.begin_recovery()=='stop'
    g=CycleGate();g.capabilities(True,True);g.state='candidate_ready';g.candidate_hash='a'*64
    assert g.begin_recovery()=='request_reboot_fastboot'
    assert g.observe_fastboot(False,True,'a')=='stop'
    g=CycleGate();g.capabilities(True,True);g.state='candidate_ready';g.candidate_hash='a'*64
    g.begin_recovery();g.observe_fastboot(True,True,'a');assert g.written('b'*64)=='stop'
    g=CycleGate();assert g.failure('abort')!='stop';assert g.failure('abort')=='stop'
    g=CycleGate();assert g.stalled('before USB enumeration')=='stop'
    print('PASS: unsupported recovery, wrong device, bad digest, repeated failure and pre-USB stall all stop.')

if __name__=='__main__':selftest()
