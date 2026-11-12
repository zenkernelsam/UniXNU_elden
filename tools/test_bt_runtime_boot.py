import copy,json,unittest,tomllib
from pathlib import Path
from bt_runtime_boot import adt,menu,FIXUP
from battery_boot import arguments
ROOT=Path(__file__).resolve().parents[1]
class RuntimeBootTests(unittest.TestCase):
    def test_only_one_adt_property_changes(self):
        before=json.loads((ROOT/'profiles/v91/boot-build/adt.json').read_text());after=adt(before)
        changes=[]
        def compare(a,b):
            self.assertEqual(len(a['properties']),len(b['properties']));self.assertEqual(len(a['children']),len(b['children']))
            for x,y in zip(a['properties'],b['properties']):
                if x!=y:
                    self.assertEqual(x['name'],'text-fixups');self.assertEqual(y['name'],x['name'])
                    self.assertEqual(bytes.fromhex(y['hex']),bytes.fromhex(x['hex'])+FIXUP);changes.append(x['name'])
            for x,y in zip(a['children'],b['children']):compare(x,y)
        compare(before['tree'],after['tree']);self.assertEqual(changes,['text-fixups'])
        with self.assertRaises(ValueError):adt(after)
    def test_bluetooth_trial_keeps_recovery_input_and_power(self):
        cfg=tomllib.loads(menu().replace('@SESSION@','0123456789abcdef'));args=arguments(cfg['entry'][0]['args'])
        for name,value in {'elden-wcn-power':'4','elden-return-fastboot':'1','elden-panic-delay':'180',
            'elden-thermal':'2','elden-usb-icl':'2','elden-ucsi-init':'2','elden-touch':'7','elden-touch-hid':'1',
            'elden-battery':'1','elden-battery-profile':'1','elden-battery-current':'-1','cpus':'8','fbwc':'1'}.items():self.assertEqual(args[name],value)
        self.assertNotIn('elden-ui-replay',args);self.assertNotIn('elden-ui',args)
        self.assertEqual(cfg['boot']['timeout'],8);self.assertEqual(cfg['boot']['default'],'xnu')
        self.assertLessEqual(len(cfg['entry'][0]['args'].encode()),1023)
if __name__=='__main__':unittest.main()
