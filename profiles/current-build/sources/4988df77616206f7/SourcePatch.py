"""Apply the exact v91 boot-stage and signature-bound Bluetooth stream delta."""
import hashlib
def patch(name,data):
    expected={'kern_exec.c':'79172c808c463e12055575d214fc6866bc9cfd30043f228936fb6a4dac8ef998',
              'ubc_subr.c':'484882286fa438f52eeabf38af199538201152b5d067746fd0d44cd6509945d8'}
    if hashlib.sha256(data).hexdigest()!=expected[name]:raise ValueError('Core source differs: '+name)
    text=data.decode()
    def replace(a,b):
        nonlocal text
        if text.count(a)!=1:raise ValueError('Ambiguous core delta')
        text=text.replace(a,b)
    if name=='kern_exec.c':
        replace('#include "EldenIconCpuStage.inc"','#include "EldenIconCpuStage.inc"\n#include "EldenBtRuntime.inc"')
        replace('\telden_icon_cpu_prepare(p, (user_addr_t)scratch_addr);','\telden_icon_cpu_prepare(p, (user_addr_t)scratch_addr);\n\telden_bt_runtime_prepare(p, (user_addr_t)scratch_addr);')
    else:
        replace('uint32_t\ncs_text_fixup_page(', '#include "EldenBtTextGuard.h"\nuint32_t\ncs_text_fixup_page(')
        marker='\t\tuint8_t *at = (uint8_t *)kva + (f->offset - page_offset);'
        guard='''		if (!strcmp(f->name, "bluetoothd")) {
			if (!elden_bt_stream_rule(f->offset, f->length, f->original, f->replacement)) continue;
			bool found = false, rejected = false;
			/* Same lockless blob lifetime as cs_validate_range: the caller's
			 * paging_in_progress reference protects this vnode/pager. No new lock. */
			for (struct cs_blob *blob = ubc_get_cs_blobs(vp); blob; blob = blob->csb_next) {
				if (blob->csb_base_offset < 0 || blob->csb_start_offset < 0 || blob->csb_end_offset <= blob->csb_start_offset) { rejected = true; break; }
				if ((uint64_t)blob->csb_base_offset > f->offset) continue;
				off_t relative = (off_t)f->offset - blob->csb_base_offset;
				if (relative < blob->csb_start_offset || relative >= blob->csb_end_offset) continue;
				found = true;
				if (blob->csb_end_offset - relative < 4 || !elden_bt_stream_identity(blob->csb_cpu_type, blob->csb_cpu_subtype, blob->csb_base_offset, blob->csb_cdhash)) rejected = true;
			}
			if (!found || rejected) continue;
		}
'''
        replace(marker,guard+marker)
    return text.encode()
