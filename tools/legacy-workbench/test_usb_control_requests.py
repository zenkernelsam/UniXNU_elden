"""Pure request validation. No device I/O or synthetic input is sent."""
import unittest
from elden_usb_console import control_request,control_ready
SESSION='1234567890abcdef'
class ControlTests(unittest.TestCase):
    def test_settings_after_released_input(self):
        ready={'fastboot':'ready'}
        self.assertFalse(control_ready('tap-settings',ready,False,False,False))
        self.assertFalse(control_ready('tap-settings',ready,True,False,False))
        self.assertTrue(control_ready('tap-settings',ready,True,True,False))
        self.assertTrue(control_ready('tap-settings',ready,False,False,True))
        self.assertFalse(control_ready('tap-settings',None,True,True,True))
        self.assertFalse(control_ready('tap-settings',{'fastboot':'not-ready'},True,True,True))
        self.assertTrue(control_ready('fastboot',ready,False,False,False))
    def test_exact_actions(self):
        for command in ('home','tap-settings','swipe-up','fastboot'):
            ident,kind,wire=control_request(dict(id='one-request',session=SESSION,command=command),SESSION)
            self.assertEqual((ident,kind),('one-request',command));self.assertEqual(wire,(command+' '+SESSION+'\n').encode())
    def test_reject_foreign_or_arbitrary(self):
        base=dict(id='one-request',session=SESSION,command='tap-settings')
        for changes in ({'session':'fedcba0987654321'},{'command':'reboot'},{'command':'tap-settings\nfastboot'},
                        {'id':'../request'},{'id':'x'*65},{'extra':'shell'},{'id':1}):
            with self.assertRaises(ValueError):control_request(dict(base,**changes),SESSION)
        with self.assertRaises(ValueError):control_request([],SESSION)
if __name__=='__main__':unittest.main()
