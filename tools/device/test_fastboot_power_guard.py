"""The low-battery case must be rejected before any EDL transition."""
import unittest
from flash_verified_boot import getvar_value,read_fastboot_power,require_fastboot_power

class PowerGuard(unittest.TestCase):
    def test_actual_depleted_device(self):
        calls=[]
        def command(*args):
            calls.append(args)
            return {'battery-voltage':'battery-voltage: 3464\nFinished. Total time: 0.003s\n',
                    'battery-soc-ok':'battery-soc-ok: no\nFinished. Total time: 0.003s\n'}[args[1]]
        power=read_fastboot_power(command)
        with self.assertRaisesRegex(RuntimeError,'3500mV'):require_fastboot_power(power)
        self.assertEqual(calls,[('getvar','battery-voltage'),('getvar','battery-soc-ok')])
    def test_native_yes(self):
        values={'battery-voltage':'(bootloader) battery-voltage: 3890\nFinished.\n','battery-soc-ok':'battery-soc-ok: yes\n'}
        power=read_fastboot_power(lambda *a:values[a[1]])
        require_fastboot_power(power)
        self.assertEqual(power,{'battery_mv':3890,'battery_soc_ok':'yes'})
    def test_unknown_and_duplicate_are_not_admitted(self):
        for value in ('FAILED (remote: unknown variable)','battery-soc-ok: yes\nbattery-soc-ok: no\n'):
            with self.assertRaises(RuntimeError):getvar_value(value,'battery-soc-ok')
    def test_bad_voltage_never_queries_soc(self):
        for voltage in ('0','-1','0x0d88','999999','n/a'):
            calls=[]
            def command(*a):calls.append(a);return 'battery-voltage: '+voltage+'\n'
            with self.assertRaises(RuntimeError):read_fastboot_power(command)
            self.assertEqual(calls,[('getvar','battery-voltage')])

if __name__=='__main__':unittest.main()
