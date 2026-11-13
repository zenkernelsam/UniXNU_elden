import unittest
from hid_delivery_boot import menu
from wait_sample_boot import template,verify


class WaitMenuTests(unittest.TestCase):
    def setUp(self):
        self.base=menu({'profile':'v106-rt-memory'})
        self.wait=template(self.base)

    def test_existing_sampler_and_exact_buffer(self):
        result=verify(self.base,self.wait)
        self.assertEqual(result['boot_args_bytes'],1023)
        self.assertFalse(result['kernel_changed'])
        self.assertIn('elden-user-wait',self.wait)

    def test_recovery_assets_and_other_fields_cannot_change(self):
        changes=[('smp-hold=600','smp-hold=0'),('elden-thermal=2','elden-thermal=0'),
                 ('Payloads/kernel.macho','Payloads/other.macho'),
                 ('timeout = 8','timeout = 0'),('id = "xnu"','id = "other"'),
                 ('elden-user-wait','elden-user-wait elden-user-wait')]
        for old,new in changes:
            with self.subTest(old=old),self.assertRaises(ValueError):
                verify(self.base,self.wait.replace(old,new,1))


if __name__=='__main__':unittest.main()
