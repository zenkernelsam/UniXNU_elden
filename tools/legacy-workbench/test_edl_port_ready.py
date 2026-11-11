"""Model only: no device I/O or flash is performed."""
from types import SimpleNamespace
import serial
from flash_verified_boot import open_verified_edl_port
good=SimpleNamespace(device='COM7',vid=0x05c6,pid=0x9008,location='1-5')
foreign=SimpleNamespace(device='COM7',vid=0x05c6,pid=0x9008,location='1-2')
class Clock:
    t=0
    def now(self):return self.t
    def sleep(self,n):self.t+=n
class Handle:
    closed=False
    def close(self):self.closed=True
c=Clock();calls=[];h=Handle()
def opening(name):
    calls.append(name)
    if len(calls)==1:raise serial.SerialException("could not open port 'COM7': FileNotFoundError(2, 'not ready')")
    return h
port,n=open_verified_edl_port(1,lambda:[good],opening,c.now,c.sleep)
assert port is h and n==2 and calls==['COM7','COM7'] and not h.closed
c=Clock();calls=[]
def busy(name):calls.append(name);raise PermissionError(5,'busy')
try:open_verified_edl_port(1,lambda:[good],busy,c.now,c.sleep)
except PermissionError:pass
else:raise AssertionError('Busy port must not be retried')
assert len(calls)==1 and c.t==0
c=Clock();calls=[]
try:open_verified_edl_port(1,lambda:[foreign],lambda name:calls.append(name),c.now,c.sleep)
except RuntimeError:pass
else:raise AssertionError('Foreign device accepted')
assert not calls
c=Clock();h=Handle();reads=[0]
def disappearing():reads[0]+=1;return [good] if reads[0]==1 else []
try:open_verified_edl_port(1,disappearing,lambda name:h,c.now,c.sleep)
except RuntimeError:pass
else:raise AssertionError('Disappeared identity accepted')
assert h.closed
print('PASS: PnP/open race retry, wrong-device rejection, busy-owner refusal, pre-protocol identity recheck')
