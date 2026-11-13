"""Hash-bound adaptation of sheng's Normal-NC real-time DRAM policy."""
import hashlib

SOURCE_SHA256 = 'e9c9caaffb8df5dfa3f840ef113fc159a0497fdddef9150bdaf4b9df44986653'


def derive(data):
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
        raise ValueError('Unexpected v91 pmap source')
    old = b'\tcase VM_WIMG_RT:\n\t\tpte = ARM_PTE_ATTRINDX(CACHE_ATTRINDX_RT);'
    new = b'''\tcase VM_WIMG_RT:
\t\t/* Elden: sheng uses Normal-NC for real-time image memory. Scope this
\t\t * to kernel-managed DRAM; peripheral/MMIO RT mappings retain the
\t\t * original policy. Apply at PTE construction/update so every alias
\t\t * agrees, with the existing cache/TLB synchronization unchanged. */
\t\tpte = ARM_PTE_ATTRINDX(pa_valid(pa) ? CACHE_ATTRINDX_WRITECOMB : CACHE_ATTRINDX_RT);'''
    if data.count(old) != 1:
        raise ValueError('RT PTE case changed')
    return data.replace(old, new)
