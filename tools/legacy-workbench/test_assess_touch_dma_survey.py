"""Synthetic parser tests only; never hardware qualification."""
import copy
import hashlib
import unittest
from assess_touch_dma_survey import IMAGE, SESSION, assess


class SurveyTest(unittest.TestCase):
    def run_case(self, extra='', change=None):
        text = f'''ELDEN STATUS v1 session={SESSION} dropped=0 cache=0 data=0 configured=1 dtr=1 init_exec=1 user_trap=1
ELDEN_TOUCH platform=0 stage=2 protocol=1 mode=4
ELDEN_TOUCH_DMA route sid=a3 slot=180 smr=800000a3 s2cr=00010000 type=1 bank=0
ELDEN_TOUCH_DMA route sid=b6 slot=181 smr=800000b6 s2cr=00000009 type=0 bank=9
ELDEN_TOUCH_DMA context sid=b6 bank=9 cbar=00010000 cba2r=00000001
ELDEN_TOUCH_DMA SMMU result=0 cr0=00000000 id0=480000c8 id1=00000010 id2=00001000 groups=200
ELDEN_TOUCH_DMA matches sid=a3 count=1 sid=b6 count=1 DMA-qualified=0
ELDEN_TOUCH stop error=0 released=0 quarantine=0 reports=0 invalid=0
{extra}'''
        if change:
            text = change(text)
        raw = text.encode()
        metadata = dict(candidate_session=SESSION, raw_sha256=hashlib.sha256(raw).hexdigest(),
                        bound_startup_milestone_observed=True)
        flash = dict(success=True, image_sha256=IMAGE, full_readback_sha256=IMAGE,
                     index=1738713600, footer_location=0, gpt_identity_checked=True)
        return raw, metadata, flash

    def test_complete_requires_more_qualification(self):
        r = assess(*self.run_case())
        self.assertTrue(r['survey_evidence_valid'])
        self.assertFalse(r['dma_qualified'])
        self.assertIn('SCTLR', r['streams']['b6']['next_step'])
        self.assertEqual(r['streams']['a3']['routes'][0]['slot'], 180)

    def test_metadata_and_flash_mismatch(self):
        for target, key in ((1, 'candidate_session'), (1, 'raw_sha256'), (2, 'full_readback_sha256')):
            items = list(copy.deepcopy(self.run_case()));items[target][key] = 'wrong'
            self.assertFalse(assess(*items)['survey_evidence_valid'])

    def test_panic_drop_truncation_and_wrong_mode(self):
        for change in (lambda s:s+'panic(cpu 0)\n',lambda s:s.replace('dropped=0','dropped=2'),
                       lambda s:s.replace('mode=4','mode=1'),lambda s:s.replace('count=1','count=2',1),
                       lambda s:s.replace('id0=480000c8','id0=ffffffff'),
                       lambda s:s.replace('init_exec=1','init_exec=0'),
                       lambda s:s.replace('released=0','released=-5'),
                       lambda s:s.replace('ELDEN_TOUCH_DMA context','lost context')):
            self.assertFalse(assess(*self.run_case(change=change))['survey_evidence_valid'])


if __name__ == '__main__':
    unittest.main()
