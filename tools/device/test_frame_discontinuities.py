import unittest,zlib
from decode_frame_preview import decode
SESSION='1234567890abcdef'
def frame(ident):
    data=bytes(180880);row='00'*476
    text=f'ELDEN_FRAME_BEGIN session={SESSION} id={ident} source=22 width=238 height=380 format=rgb565le\n'
    text+=''.join(f'ELDEN_FRAME_ROW id={ident} y={y} hex={row}\n' for y in range(380))
    text+=f'ELDEN_FRAME_END id={ident} crc32={zlib.crc32(data):08x} bytes=180880\n'
    return text.encode()
class DiscontinuityTests(unittest.TestCase):
    def test_only_whole_post_gap_frames_qualify(self):
        prefix=frame(1)[:4000];data=prefix+b'fragment of lost row\n'+frame(2)
        result=decode(data,SESSION,[len(prefix)])
        self.assertEqual([x[0] for x in result],[2])
    def test_complete_before_and_after(self):
        before=frame(1);data=before+b'incomplete'+b'rest\n'+frame(2)
        self.assertEqual([x[0] for x in decode(data,SESSION,[len(before)+10])],[1,2])
    def test_invalid_or_repeated_segments(self):
        data=frame(1)
        for gaps in [[-1],[len(data)+1],[True],[20,10]]:
            with self.assertRaises(ValueError):decode(data,SESSION,gaps)
        with self.assertRaises(ValueError):decode(data+data,SESSION,[len(data)])
if __name__=='__main__':unittest.main()
