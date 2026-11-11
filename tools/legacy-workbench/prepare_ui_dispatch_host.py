from pathlib import Path
import shutil
r=Path(__file__).resolve().parents[1];p=r/'research/ui-dispatch-20261111'
s=(p/'kdebug-dispatch.c').read_text()
parts=[s.split('#if defined(XNU_ELDEN_UI_DISPATCH) && XNU_ELDEN_UI_DISPATCH\n',1)[1].split('#endif',1)[0]]
for name in ('typefilter_copy','typefilter_reject_all','typefilter_allow_all','typefilter_allow_class','typefilter_allow_csc','typefilter_is_debugid_allowed'):
 start=s.index(name+'(');brace=s.index('{',start);end=brace+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 parts.append(('static bool\n' if name.endswith('allowed') else 'static void\n')+s[start:end])
(p/'typefilter-host.inc').write_text('\n'.join(parts)+'\n')
for name in ('EldenUiDispatch.inc','EldenUiDispatchCore.h'):
 s=(Path('D:/Desktop/Git/UniXNU_elden/patches/xnu')/name).read_text()
 if name.endswith('.inc'):s=s.replace('#include <kern/thread_call.h>','').replace('#include <pexpert/pexpert.h>','')
 (p/name).write_text(s)
source=(p/'kdebug-dispatch.c').read_text();start=source.index('kdebug_commpage_state(void)\n{');brace=source.index('{',start);end=brace+1;depth=1
while depth:
 depth+=(source[end]=='{')-(source[end]=='}');end+=1
(p/'commpage-state-host.inc').write_text('static uint32_t\n'+source[start:end]+'\n')
print('Host adapter uses actual candidate code and extracted filter helpers.')
