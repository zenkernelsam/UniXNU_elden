"""Export exact Y700 GPU DTS evidence. This never creates runnable MMIO inputs."""
from pathlib import Path
import argparse, hashlib, json, re, subprocess

PIN='6f97250ead155fc3b980856323e99a4623dae73d'
FILES=['arch/arm64/boot/dts/qcom/sm8750.dtsi','arch/arm64/boot/dts/qcom/sm8750-lenovo-elden.dts']

def blob(repo,name):
    return subprocess.check_output(['git','-C',str(repo),'show',PIN+':'+name])

def block(text,label):
    m=re.search(r'(?m)^\s*'+re.escape(label)+r':\s*[^\n{]+\{',text)
    if not m:raise ValueError('DTS label missing: '+label)
    start=m.end();depth=1;at=start
    while depth and at<len(text):
        depth+=(text[at]=='{')-(text[at]=='}');at+=1
    if depth:raise ValueError('Unbalanced fixed DTS block')
    return text[m.start():at],text[:m.start()].count('\n')+1

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--linux',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();sources={name:blob(a.linux,name) for name in FILES};base=sources[FILES[0]].decode()
    board=sources[FILES[1]].decode()
    if 'lenovo,elden' not in board or 'qcom,sm8750' not in board:raise ValueError('Not the pinned Elden board')
    nodes={}
    for label in ('gpu','gmu','gpucc','adreno_smmu'):
        text,line=block(base,label);props={}
        for key in ('compatible','reg','reg-names','interrupts','interrupt-names','iommus','nvmem-cell-names'):
            m=re.search(r'(?m)^\s*'+re.escape(key)+r'\s*=([^;]+);',text)
            if m:props[key]=' '.join(m.group(1).split())
        nodes[label]={'source':FILES[0],'line':line,'properties_as_written':props,'source_block_sha256':hashlib.sha256(text.encode()).hexdigest()}
    result={'device':'Lenovo TB322FC / elden','soc':'SM8750','linux_commit':PIN,
            'source_url':'https://github.com/h0cheung/tb322fc-linux-kernel',
            'files':{name:hashlib.sha256(data).hexdigest() for name,data in sources.items()},'nodes':nodes,
            'status':'Unresolved DTS property text, not a flattened runtime tree or hardware observation',
            'runtime_enabled':False,'device_access':False,
            'still_required':['stock GPU chip/revision and firmware hashes','actual reserved memory/loader ownership','live SMMU routes/CB ownership and address widths','board cmd-db rails and bandwidth votes','Y700 speed-bin/thermal ceilings','framebuffer/Metal bridge lifetime and fallback','native callbacks and full link/ABI audit']}
    a.output.parent.mkdir(parents=True,exist_ok=True)
    with a.output.open('x',encoding='utf-8') as f:json.dump(result,f,indent=2);f.write('\n')
    print('Recorded 4 pinned DTS GPU nodes; not runtime configuration.')
if __name__=='__main__':main()
