"""Generate the opt-in elden GOP console patch from the current XNU tree.

Only writes a device patch, not the checked-out XNU source. The runtime
argument distinguishes a UEFI handoff from an Apple iBoot panic-log handoff.
"""
import argparse
import difflib
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    changes = []
    file = 'pexpert/arm/pe_init.c'
    old = (a.source / file).read_text()
    marker = 'PE_create_console(void)\n{\n'
    assert old.count(marker) == 1
    new = old.replace(marker, marker + '''	int elden_gop_console = 0;
	PE_parse_boot_argn("elden-gop-console", &elden_gop_console, sizeof(elden_gop_console));
	if (elden_gop_console) {
		/* UEFI supplied boot_args.Video, not Apple's fixed panic-log area.
		 * Bit 0 is initialize_screen's physical-address marker. */
		extern void PE_elden_enable_gop_kprintf(void);
		PE_state.video.v_baseAddr |= 1;
		PE_initialize_console(&PE_state.video, kPETextMode);
		PE_elden_enable_gop_kprintf();
		return;
	}

''', 1)
    changes.extend(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                                      fromfile='a/' + file, tofile='b/' + file))
    file = 'pexpert/arm/pe_kprintf.c'
    old = (a.source / file).read_text()
    marker = 'static void serial_putc_crlf(char c);\n'
    assert old.count(marker) == 1
    new = old.replace(marker, marker + '''
void
PE_elden_enable_gop_kprintf(void)
{
	disable_serial_output = false;
	disable_kprintf_output = false;
	PE_kputc = console_write_unbuffered;
}
''', 1)
    marker = 'PE_init_kprintf(void)\n{\n'
    assert new.count(marker) == 1
    new = new.replace(marker, marker + '''	int elden_gop_console = 0;
	PE_parse_boot_argn("elden-gop-console", &elden_gop_console, sizeof(elden_gop_console));
	if (elden_gop_console) {
		PE_elden_enable_gop_kprintf();
		return;
	}
''', 1)
    changes.extend(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                                      fromfile='a/' + file, tofile='b/' + file))
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(''.join(changes))
    print(a.output)


if __name__ == '__main__':
    main()
