"""No device I/O: USB chunks larger than fragment limit must retain controls."""
import unittest
from elden_usb_console import ConsoleLines

class LinesTest(unittest.TestCase):
    def test_large_chunk_controls_survive(self):
        wanted=['ELDEN_HID event-driver=1 virtual-source=1',
                'ELDEN_DISPLAY frame=7 kind=0 pixels=70000 bounds=0,0-1903,3039']
        wanted+=['ELDEN_FRAME_ROW id=1 y=%d hex=%s'%(i,'ab'*476) for i in range(80)]
        wanted+=['ELDEN STATUS v1 session=1234567890abcdef cache=0 data=0']
        data='\n'.join(wanted)+'\n';p=ConsoleLines();got=[]
        for offset in range(0,len(data),32768):got+=p.feed(data[offset:offset+32768])
        self.assertEqual(got,wanted);self.assertEqual(p.discarded,0)
    def test_fragment(self):
        p=ConsoleLines();self.assertEqual(p.feed('ELDEN STA'),[])
        self.assertEqual(p.feed('TUS v1\nnext\n'),['ELDEN STATUS v1','next'])
    def test_oversize_fragment_not_reinterpreted(self):
        p=ConsoleLines(12);self.assertEqual(p.feed('x'*13),[])
        self.assertEqual(p.feed('fake status'),[])
        self.assertEqual(p.feed('end\nreal\n'),['real']);self.assertEqual(p.discarded,1)

if __name__=='__main__':unittest.main()
