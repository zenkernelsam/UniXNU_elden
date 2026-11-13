"""Frozen native4K bootstrap capacity change for an 8GiB allocation."""
import hashlib
HASHES={'pmap.h':'0e08e561df74ee4cf838cda9c95838e588d1d90f16cb5531b57aec08f0be4c7c',
        'assym.s':'b965fb1086420d08591a2e40cfa099b2018d3046b15137bf8d063c9a2ede401d',
        'arm_vm_init.c':'372d0449a312dc4391aa1e6b5adee35dd2a87b4c7beb0000954a3d84808493e3'}
def derive(name,data):
    if hashlib.sha256(data).hexdigest()!=HASHES[name]:raise ValueError('Unexpected memory input: '+name)
    if name=='pmap.h':
        old=b'''/* Native 4 KiB: two roots plus up to seven L2 pages per 6 GiB mapping.
 * The physical and virtual bases may each straddle an extra 1 GiB region.
 */
#define BOOTSTRAP_TABLE_SIZE (ARM_PGBYTES * 16)'''
        new=b'''/* Native 4 KiB: 8 GiB identity/linear maps need at most 20 pages.
 * Reserve 24 to also satisfy arm_vm_init's conservative aperture/slide
 * bound. Actual RAM must still be allocated by firmware.
 */
#define BOOTSTRAP_TABLE_SIZE (ARM_PGBYTES * 24)'''
        if data.count(old)!=1:raise ValueError('Bootstrap size branch differs')
        return data.replace(old,new)
    if name=='assym.s':
        for key in ('BOOTSTRAP_TABLE_BYTES','BOOTSTRAP_TABLE_BYTES_NUM'):
            old=('#define '+key+' 65536\n').encode();new=('#define '+key+' 98304\n').encode()
            if data.count(old)!=1:raise ValueError('Generated bootstrap constant differs')
            data=data.replace(old,new)
        return data
    old=b'\tif (pages_used > BOOTSTRAP_TABLE_SIZE) {'
    new=b'''#if !defined(APPLE_ARM64_ARCH_FAMILY) && !__ARM_16K_PG__
\t/* Compare pages with pages; the pool macro is measured in bytes. */
\tif (pages_used > (BOOTSTRAP_TABLE_SIZE / ARM_PGBYTES)) {
#else
\tif (pages_used > BOOTSTRAP_TABLE_SIZE) {
#endif'''
    if data.count(old)!=1:raise ValueError('Bootstrap sanity check differs')
    # The original bound estimates the aperture, larger than the initial
    # identity/linear mapping. It is conservative; never remove the check.
    return data.replace(old,new)
