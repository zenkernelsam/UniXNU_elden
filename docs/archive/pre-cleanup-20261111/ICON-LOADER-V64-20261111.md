# Icon loader continuation

v63 captured valid swipe release and subsequent tap/down/up events. The test reached the original IconServices GL fault. The environment hook had a concrete mismatch: XNU's exec_save_path stores EXECUTABLE_KEY plus the path, not a bare path.

v64 fixed that contract and passed corresponding host tests. Hardware observed13 environment appends, but also13 original GL faults; the installed marker and Settings content were not verified. Preferences appeared in periodic samples. The corrected one-swipe/one-tap host sequence and automatic return passed; exact stock Android was restored.

The original dyld identifies as1165.3. Matching Apple source gates DYLD_INSERT_LIBRARIES on AMFI path-variable permission; iOS does not remove these variables as macOS does. Runtime AMFI values and library mapping have not been observed, so policy rejection is a hypothesis, not a measured result. The original service has no __RESTRICT segment and its getter ABI was checked against the original method table.

The paired workbench prepared an undeployed normal LC_LOAD_DYLIB dependency prototype. It uses spare header space, preserves every original __TEXT section and entitlement, and passes ad-hoc signature verification. Its dependency is placed under the original service's permitted cache directory. RAM-only executable redirection, trust-cache addition and reliable installation-status reporting remain to be implemented. Do not flash this prototype or introduce global AMFI/dyld policy overrides. The original persistent system files are unchanged. Full charging, rendering, physical touch and other peripherals remain incomplete.
