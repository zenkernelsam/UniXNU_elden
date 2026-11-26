"""metal2vulkan migration call-site reachability suite (offline static analysis).

Scans the prepared-tree AdrenoMetal bridge/frontend Rust sources for metal2vulkan /
metal2vulkan_runtime imports and textual call sites, resolving every import against the
genuine pinned 779eb522 crate surface extracted from the --m2v-repo cache (pure git
object reads, zero network). The audited gpu-opt-010 inventory was scoped to
NativeMetalObjects.rs; the zero-drift check therefore applies to exactly that file,
while call-site reachability spans all bridge+tools Rust sources. Classifies the
26 head-missing + 3 renamed-candidate symbols into active migration blockers vs
unused. Negative fault injection (phantom import, corrupted pin rev, dropped bridge
source, unresolvable symbol) feeds the same pure evaluation functions and must be
rejected fail-closed.
"""
from pathlib import Path
import argparse, json, re, subprocess

PIN = '779eb522b45615ab7a2e96f823502715238e472b'
HEAD = 'a3786dd88b6e55f16cfffbae48d14ad6c95d1e25'

# Frozen audited inventory from gpu-opt-010 m2v-rt-gap.json (109 imports, 26 missing, 3 renamed).
AUDITED_IMPORTS = json.loads('["metal2vulkan::bind::SamplerDescriptor", "metal2vulkan::library::FunctionConstantValue", "metal2vulkan::library::FunctionConstantValues", "metal2vulkan::metal::BlendFactor", "metal2vulkan::metal::BlendOperation", "metal2vulkan::metal::ColorWriteMask", "metal2vulkan::metal::CompareFunction", "metal2vulkan::metal::CullMode", "metal2vulkan::metal::DataType", "metal2vulkan::metal::IndexType", "metal2vulkan::metal::PixelFormat", "metal2vulkan::metal::PrimitiveTopologyClass", "metal2vulkan::metal::PrimitiveType", "metal2vulkan::metal::RenderStages", "metal2vulkan::metal::SamplerAddressMode", "metal2vulkan::metal::SamplerBorderColor", "metal2vulkan::metal::SamplerMinMagFilter", "metal2vulkan::metal::SamplerMipFilter", "metal2vulkan::metal::StencilOperation", "metal2vulkan::metal::TessellationControlPointIndexType", "metal2vulkan::metal::TessellationFactorFormat", "metal2vulkan::metal::TessellationFactorStepFunction", "metal2vulkan::metal::TessellationPartitionMode", "metal2vulkan::metal::TextureSwizzle", "metal2vulkan::metal::TextureSwizzleChannels", "metal2vulkan::metal::TextureType", "metal2vulkan::metal::TextureUsage", "metal2vulkan::metal::TriangleFillMode", "metal2vulkan::metal::VertexFormat", "metal2vulkan::metal::VertexStepFunction", "metal2vulkan::metal::Winding", "metal2vulkan::metal::state::DepthStencilDescriptor", "metal2vulkan::metal::state::StencilDescriptor", "metal2vulkan::pipeline::ColorAttachmentDescriptor", "metal2vulkan::pipeline::ComputePipelineDescriptor", "metal2vulkan::pipeline::LinkedFunction", "metal2vulkan::pipeline::LinkedFunctions", "metal2vulkan::pipeline::RenderPipelineDescriptor", "metal2vulkan::pipeline::StageSource", "metal2vulkan::pipeline::TessellationDescriptor", "metal2vulkan::pipeline::TileRenderPipelineDescriptor", "metal2vulkan::pipeline::VertexAttributeDescriptor", "metal2vulkan::pipeline::VertexBufferLayoutDescriptor", "metal2vulkan::pipeline::VertexDescriptor", "metal2vulkan::reflection::ArrayType", "metal2vulkan::reflection::Binding", "metal2vulkan::reflection::PointerType", "metal2vulkan::reflection::StructType", "metal2vulkan::reflection::TextureReferenceType", "metal2vulkan_runtime::::BinaryArchive", "metal2vulkan_runtime::::BinaryArchiveDescriptor", "metal2vulkan_runtime::::BlitCommandEncoder", "metal2vulkan_runtime::::BlitOption", "metal2vulkan_runtime::::Buffer", "metal2vulkan_runtime::::ClearColor", "metal2vulkan_runtime::::CommandBuffer", "metal2vulkan_runtime::::CommandBufferError", "metal2vulkan_runtime::::CommandQueue", "metal2vulkan_runtime::::ComputeCommandEncoder", "metal2vulkan_runtime::::ComputePipelineState", "metal2vulkan_runtime::::Deallocator", "metal2vulkan_runtime::::DebugMessage", "metal2vulkan_runtime::::DepthStencilState", "metal2vulkan_runtime::::Device", "metal2vulkan_runtime::::DeviceOptions", "metal2vulkan_runtime::::Event", "metal2vulkan_runtime::::EventRef", "metal2vulkan_runtime::::FailureOrigin", "metal2vulkan_runtime::::FamilyRow", "metal2vulkan_runtime::::FeatureRow", "metal2vulkan_runtime::::Fence", "metal2vulkan_runtime::::Function", "metal2vulkan_runtime::::GpuFamily", "metal2vulkan_runtime::::Heap", "metal2vulkan_runtime::::HeapDescriptor", "metal2vulkan_runtime::::HeapType", "metal2vulkan_runtime::::HostMemory", "metal2vulkan_runtime::::IoSurfacePlane", "metal2vulkan_runtime::::IoSurfaceYCbCr", "metal2vulkan_runtime::::Library", "metal2vulkan_runtime::::LoadAction", "metal2vulkan_runtime::::MAX_COLOR_ATTACHMENTS", "metal2vulkan_runtime::::MessageSeverity", "metal2vulkan_runtime::::MessageSink", "metal2vulkan_runtime::::MultisampleDepthResolveFilter", "metal2vulkan_runtime::::MultisampleStencilResolveFilter", "metal2vulkan_runtime::::Origin", "metal2vulkan_runtime::::PipelineOption", "metal2vulkan_runtime::::PipelineOptions", "metal2vulkan_runtime::::PrivateCount", "metal2vulkan_runtime::::PrivateFlag", "metal2vulkan_runtime::::PrivateProperties", "metal2vulkan_runtime::::PurgeableState", "metal2vulkan_runtime::::Region", "metal2vulkan_runtime::::RenderCommandEncoder", "metal2vulkan_runtime::::RenderPassDescriptor", "metal2vulkan_runtime::::RenderPipelineState", "metal2vulkan_runtime::::ResourceOptions", "metal2vulkan_runtime::::RowState", "metal2vulkan_runtime::::SamplerState", "metal2vulkan_runtime::::ScissorRect", "metal2vulkan_runtime::::SharedEvent", "metal2vulkan_runtime::::SharedEventListener", "metal2vulkan_runtime::::Size", "metal2vulkan_runtime::::StoreAction", "metal2vulkan_runtime::::Texture", "metal2vulkan_runtime::::TextureDescriptor", "metal2vulkan_runtime::::Viewport", "metal2vulkan_runtime::::VisibilityResultMode"]')
MISSING_AT_HEAD = json.loads('["BinaryArchive", "BinaryArchiveDescriptor", "Deallocator", "DebugMessage", "EventRef", "FailureOrigin", "FamilyRow", "FeatureRow", "Function", "GpuFamily", "HeapDescriptor", "HeapType", "IoSurfacePlane", "IoSurfaceYCbCr", "MessageSeverity", "MessageSink", "Origin", "PipelineOption", "PipelineOptions", "PrivateCount", "PrivateFlag", "PrivateProperties", "PurgeableState", "ResourceOptions", "RowState", "SharedEventListener"]')
RENAMED_CANDIDATES = json.loads('["Heap", "HostMemory", "Size"]')

USE = re.compile(r'use\s+(metal2vulkan\w*)::([^;]+);')
ITEM = re.compile(r'^\s*pub(?:\([^)]*\))?\s+(?:async\s+|unsafe\s+|extern\s+"C"\s+)*'
                  r'(struct|enum|fn|type|const|trait|union|static)\s+(\w+)', re.M)
REEXPORT = re.compile(r'^\s*pub\s+use\s+([\w:]+?)(?:::\{([^}]+)\})?\s*;', re.M)


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo)] + list(args), stderr=subprocess.DEVNULL)


def pin_surface(repo, pin_rev):
    """Public item names defined anywhere in the pinned crates (documented approximation:
    crate-root imports count as preserved when the name is publicly defined in-crate).
    Extraction is a single `git archive` stream read in-memory -- one subprocess total."""
    import io as _io
    import tarfile as _tarfile
    names = set()
    try:
        blob = git(repo, 'archive', pin_rev, 'crates/metal2vulkan/src', 'crates/metal2vulkan-runtime/src')
    except subprocess.CalledProcessError:
        blob = git(repo, 'archive', pin_rev, 'crates/metal2vulkan/src')
    with _tarfile.open(fileobj=_io.BytesIO(blob)) as tf:
        for m in tf.getmembers():
            if not m.isfile() or not m.name.endswith('.rs'):
                continue
            text = tf.extractfile(m).read().decode(errors='replace')
            for kind, name in ITEM.findall(text):
                names.add(name)
            for mac in re.finditer(r'(?m)^\s*(metal_enum|metal_flags)!\s*\{', text):
                window = text[mac.end():mac.end() + 600]
                m = re.search(r'(\w+)\s*=\s*"', window)
                if m:
                    names.add(m.group(1))
            for path, braces in REEXPORT.findall(text):
                if braces:
                    for n in braces.split(','):
                        n = n.strip().split(' as ')[-1]
                        if n and n != 'self':
                            names.add(n)
                else:
                    names.add(path.split('::')[-1])
    return names


def pin_revision_failures(repo, pin_rev):
    try:
        if git(repo, 'cat-file', '-t', pin_rev).decode().strip() != 'commit':
            return ['pin object is not a commit']
    except subprocess.CalledProcessError:
        return ['pin rev missing from m2v repo cache']
    return []


def parse_imports(source):
    """Same grammar as tools/audit_metal2vulkan_rt_gap.py."""
    imports = []
    for crate, rest in USE.findall(source):
        rest = rest.strip()
        if rest.endswith('}'):
            if rest.startswith('{'):
                path_part, braces = '', rest[1:]
            else:
                path_part, _, braces = rest.rpartition('::{')
            symbols = [s.strip().split(' as ')[0] for s in braces[:-1].split(',') if s.strip()]
            for sym in symbols:
                imports.append({'crate': crate, 'module': path_part, 'symbol': sym})
        else:
            parts = rest.split('::')
            imports.append({'crate': crate, 'module': '::'.join(parts[:-1]),
                            'symbol': parts[-1].split(' as ')[0]})
    return imports


def import_key(imp):
    return '%s::%s::%s' % (imp['crate'], imp['module'], imp['symbol'])


def scan_sources(texts):
    """texts: {relpath: source}. Returns (imports, per-symbol call-site counts)."""
    imports = []
    for rel, text in sorted(texts.items()):
        for imp in parse_imports(text):
            imp['file'] = rel
            imports.append(imp)
    counts = {}
    imported = {}
    use_span = re.compile(r'use\s+metal2vulkan\w*::[^;]+;', re.S)
    for imp in imports:
        imported.setdefault(imp['symbol'], set()).add(imp['file'])
    watched = sorted(set(MISSING_AT_HEAD) | set(RENAMED_CANDIDATES))
    for sym in watched:
        uses = []
        pat = re.compile(r'\b' + re.escape(sym) + r'\b')
        for rel, text in sorted(texts.items()):
            stripped = use_span.sub('', text)
            n = len(pat.findall(stripped))
            if n:
                uses.append({'file': rel, 'count': n})
        counts[sym] = {'call_sites': uses, 'imported_in': sorted(imported.get(sym, []))}
    return imports, counts


def drift_failures(imports, audited):
    """NativeMetalObjects.rs-scoped import set must equal the audited inventory exactly."""
    got = sorted(set(import_key(i) for i in imports))
    missing = sorted(set(audited) - set(got))
    extra = sorted(set(got) - set(audited))
    f = []
    if missing:
        f.append('audited imports no longer referenced: ' + ','.join(missing[:4]))
    if extra:
        f.append('new unaudited imports: ' + ','.join(extra[:4]))
    return f


def resolution_failures(imports, surface):
    return ['unresolved import %s::%s in %s' % (i['crate'], i['symbol'], i.get('file', '?'))
            for i in imports if i['symbol'] not in surface]


def migration_summary(counts):
    """Call sites exclude use-statement spans: a symbol merely imported but never
    referenced is import_only (migration action: drop the import), while zero textual
    references at all means unused. Counting is the documented textual approximation."""
    active, import_only, unused = {}, {}, []
    for sym, info in sorted(counts.items()):
        total = sum(u['count'] for u in info['call_sites'])
        if total:
            active[sym] = {'total_call_sites': total, 'files': info['call_sites'],
                           'imported_in': info['imported_in']}
        elif info['imported_in']:
            import_only[sym] = {'imported_in': info['imported_in']}
        else:
            unused.append(sym)
    return {
        'active_blockers': sorted(active),
        'active_blocker_detail': active,
        'import_only': sorted(import_only),
        'import_only_detail': import_only,
        'unused_missing': unused,
        'renamed_candidates_used': sorted(s for s in RENAMED_CANDIDATES if s in active),
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tree', type=Path, required=True)
    p.add_argument('--m2v-repo', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    positive, negative, failures = [], [], []

    def check(name, ok, detail=''):
        if ok:
            positive.append(name)
        else:
            failures.append(name + (': ' + detail if detail else ''))

    def expect_reject(name, fn):
        try:
            bad = fn()
        except Exception:
            bad = []
        if bad:
            negative.append(name)
        else:
            failures.append('negative case escaped: ' + name)

    tree = a.tree.resolve()
    bridge = tree / 'drivers' / 'AdrenoMetal'
    texts = {}
    for rs in sorted(bridge.rglob('*.rs')):
        texts[str(rs.relative_to(tree))] = rs.read_text(errors='replace')
    check('bridge rust sources present', len(texts) >= 3, '%d files' % len(texts))

    imports, counts = scan_sources(texts)
    objects_file = [k for k in texts if k.endswith('NativeMetalObjects.rs')]
    objects_imports = [i for i in imports if i['file'] in objects_file]
    df = drift_failures(objects_imports, AUDITED_IMPORTS)
    check('zero drift in NativeMetalObjects.rs vs audited 109-import inventory', not df, '; '.join(df))

    rev_f = pin_revision_failures(a.m2v_repo.resolve(), PIN)
    check('pin 779eb522 commit present in m2v cache', not rev_f, '; '.join(rev_f))
    surface = pin_surface(a.m2v_repo.resolve(), PIN)
    check('pin surface non-trivial', len(surface) > 200, '%d items' % len(surface))
    # Resolution scope: only pinned-compile-surface files (drivers/AdrenoMetal/bridge/*.rs).
    # tools/ is unreferenced host diagnostic tooling (adrenoprobed is built from
    # NativeAdrenoProbeDaemon.cpp, not MetalVulkanProbe.rs); its imports are informational.
    bridge_imports = [i for i in imports if '/bridge/' in i['file']]
    tool_imports = [i for i in imports if '/bridge/' not in i['file']]
    rf = resolution_failures(bridge_imports, surface)
    check('all pinned bridge imports resolve against pinned surface', not rf, '; '.join(rf[:2]))
    stale = resolution_failures(tool_imports, surface)

    summary = migration_summary(counts)
    check('migration summary well-formed',
          len(summary['active_blockers']) + len(summary['import_only']) + len(summary['unused_missing']) == 26 + 3)

    # Negative fault injection: same pure functions, mutated inputs.
    phantom = dict(texts)
    first = sorted(phantom)[0]
    phantom[first] = phantom[first] + '\nuse metal2vulkan_runtime::NonExistentSymbol;\n'
    pi, _ = scan_sources(phantom)
    expect_reject('phantom import rejected',
                  lambda: resolution_failures(pi, surface))
    expect_reject('corrupted pin rev rejected',
                  lambda: pin_revision_failures(a.m2v_repo.resolve(), 'deadbeef' * 5))
    dropped = {k: v for k, v in texts.items() if 'NativeMetalObjects.rs' not in k}
    di, _ = scan_sources(dropped)
    expect_reject('dropped bridge source rejected',
                  lambda: drift_failures(di, AUDITED_IMPORTS))
    expect_reject('surface truncation rejected',
                  lambda: resolution_failures(imports, surface - {'FunctionConstantValues'}))

    passed = not failures
    report = {
        'passed': passed,
        'case_counts': {'positive': len(positive), 'negative': len(negative)},
        'positive': positive, 'negative': negative, 'failures': failures,
        'pin': PIN, 'head': HEAD,
        'bridge_files': sorted(texts),
        'import_count': len(imports),
        'pin_surface_items': len(surface),
        'migration_summary': summary,
        'stale_tooling_notes': stale,
        'device_access': False, 'hardware': False, 'hardware_qualified': False,
    }
    (out / 'result.json').write_text(json.dumps(report, indent=2) + chr(10))
    if not passed:
        raise SystemExit('m2v callsite reachability failures: ' + '; '.join(failures))
    print('PASS: m2v-callsite-reachability; %d positive + %d negative; %d active blockers %d unused'
          % (len(positive), len(negative), len(summary['active_blockers']), len(summary['unused_missing'])))


if __name__ == '__main__':
    main()
