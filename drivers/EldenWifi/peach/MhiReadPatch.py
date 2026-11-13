"""Preserve failed MHI status reads instead of treating them as completion."""
import hashlib
EXPECTED='2a7d8ec943d9a88d7474da5b8faa40cd520e22cb6757e24391be91006d0f9644'
def patch(data):
    if hashlib.sha256(data).hexdigest()!=EXPECTED:raise ValueError('Prepared MHI boot source differs')
    text=data.decode()
    for name,identifier in [('mhi_fw_load_bhie','sequence_id'),('mhi_fw_load_bhi','session_id')]:
        start=text.index('static int '+name+'(');end=text.index('\n}',start)+2
        body=text[start:end]
        def replace(old,new):
            nonlocal body
            if body.count(old)!=1:raise ValueError('MHI read patch anchor changed')
            body=body.replace(old,new)
        replace('u32 tx_status, '+identifier+';','u32 tx_status = 0, '+identifier+';')
        replace('\tint ret;','\tint ret, read_error = 0;')
        replace('mhi_read_reg_field(mhi_cntrl, base,','(read_error = mhi_read_reg_field(mhi_cntrl, base,')
        replace('&tx_status) || tx_status,','&tx_status)) || tx_status,')
        if name=='mhi_fw_load_bhie':
            replace('''\tif (MHI_PM_IN_ERROR_STATE(mhi_cntrl->pm_state) ||
\t    tx_status != BHIE_TXVECSTATUS_STATUS_XFER_COMPL)
\t\treturn -EIO;''','''\tif (MHI_PM_IN_ERROR_STATE(mhi_cntrl->pm_state))
\t\treturn -EIO;
\tif (read_error)
\t\treturn read_error;
\tif (tx_status != BHIE_TXVECSTATUS_STATUS_XFER_COMPL)
\t\treturn -EIO;''')
        else:
            replace('''\tif (tx_status == BHI_STATUS_ERROR) {''','''\tif (read_error)
\t\treturn read_error;

\tif (tx_status == BHI_STATUS_ERROR) {''')
        text=text[:start]+body+text[end:]
    return text.encode()
