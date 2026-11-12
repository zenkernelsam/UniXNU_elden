"""The only boot-menu difference allowed by the framebuffer experiment."""
from pathlib import Path
import hashlib,json,tomllib
ROOT=Path(__file__).resolve().parents[1]

def template(enabled=False):
    text=(ROOT/'profiles/v91/boot-build/unixnu.toml.in').read_text()
    if not enabled:return text
    config=tomllib.loads(text)
    first=config['entry'][0]
    if first['id']!='xnu' or 'fbwc' in first['args'].split():raise ValueError('Unexpected baseline menu')
    before='args = "'+first['args']+'"'
    if text.count(before)!=1:raise ValueError('Ambiguous primary entry')
    # The pinned XNU parser interprets a bare value-less key as numeric1.
    # 1018 existing bytes + " fbwc" =1023, leaving the terminating NUL.
    text=text.replace(before,'args = "'+first['args']+' fbwc"',1)
    if len(tomllib.loads(text.replace('@SESSION@','0'*16))['entry'][0]['args'].encode())>1023:
        raise ValueError('Framebuffer option exceeds the real boot-args field')
    return text

def qualify(report_path,entry_path):
    report=json.loads(Path(report_path).read_text());entry=json.loads(Path(entry_path).read_text())
    name=report.get('profile','v93-fbwc')
    if name not in ('v93-fbwc','v94-fbwc-observation','v95-fbwc-pairs'):raise ValueError('Unknown framebuffer profile')
    expected=hashlib.sha256((ROOT/'profiles'/name/'plan.json').read_bytes()).hexdigest()
    if report['profile_sha256']!=expected or report['recompiled_objects']!=5 or report['original_apple_executables_hashed']!=30:
        raise ValueError('Unqualified framebuffer build')
    if entry['source_sha256']!=report['kernel_sha256']:raise ValueError('Entry image is not the framebuffer kernel')
    return {'profile':name,'profile_sha256':expected,'pre_entry_kernel_sha256':report['kernel_sha256'],'boot_option':'fbwc'}
