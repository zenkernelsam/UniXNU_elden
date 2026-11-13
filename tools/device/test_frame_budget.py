"""Keep late Settings previews within the target's twelve-frame budget."""
import unittest

from decode_frame_preview import decode
from test_frame_discontinuities import SESSION, frame


class FrameBudgetTests(unittest.TestCase):
    def test_all_twelve_and_late_settings_frames(self):
        data = b''.join(frame(i) for i in range(1, 13))
        self.assertEqual([f[0] for f in decode(data, SESSION)], list(range(1, 13)))
        self.assertEqual([f[0] for f in decode(frame(11) + frame(12), SESSION)], [11, 12])

    def test_out_of_budget_or_duplicate_rejected(self):
        for data in (frame(0), frame(13), frame(12) + frame(12)):
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                decode(data, SESSION)

    def test_late_frames_still_require_session_rows_and_crc(self):
        good = frame(12)
        with self.assertRaisesRegex(ValueError, 'session mismatch'):
            decode(good.replace(SESSION.encode(), b'ffffffffffffffff'), SESSION)
        missing = b'\n'.join(line for line in good.split(b'\n')
                             if not line.startswith(b'ELDEN_FRAME_ROW id=12 y=379 '))
        self.assertEqual(decode(missing, SESSION), [])
        corrupt = good.replace(b'y=0 hex=00', b'y=0 hex=01', 1)
        with self.assertRaisesRegex(ValueError, 'CRC32 mismatch'):
            decode(corrupt, SESSION)

    def test_late_frames_do_not_join_across_gaps(self):
        broken = frame(11)
        joined = broken + frame(12)
        self.assertEqual([f[0] for f in decode(joined, SESSION, [4000])], [12])
        with self.assertRaisesRegex(ValueError, 'Repeated frame'):
            decode(frame(12) + frame(12), SESSION, [len(frame(12))])


if __name__ == '__main__':
    unittest.main()
