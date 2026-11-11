import unittest,zlib
from decode_frame_preview import decode
SESSION='1234567890abcdef'
class FrameReplyTests(unittest.TestCase):
    def frame(self,ack_session,command='HOME'):
        raw=bytes(180880);row='00'*476
        out=[f'ELDEN_FRAME_BEGIN session={SESSION} id=5 source=22 width=238 height=380 format=rgb565le\n']
        for y in range(380):
            pixels=row
            if y==0:pixels=row[:899]+f'\r\nELDEN ACK {command} session={ack_session}; queued\r\n'+row[899:]
            out.append(f'ELDEN_FRAME_ROW id=5 y={y} hex={pixels}\n')
        out.append(f'ELDEN_FRAME_END id=5 crc32={zlib.crc32(raw):08x} bytes=180880\n')
        return ''.join(out).encode()
    def test_complete_bound_ack(self):
        for command in ('HOME','TAP_SETTINGS','SWIPE_UP','FASTBOOT','CHARGE'):
            frames=decode(self.frame(SESSION,command),SESSION)
            self.assertEqual(len(frames),1);self.assertEqual(frames[0][2],bytes(180880))
    def test_foreign_ack_not_stripped(self):
        with self.assertRaises(ValueError):decode(self.frame('ffffffffffffffff'),SESSION)
if __name__=='__main__':unittest.main()
