# v92: unqualified framebuffer experiment

These eight source files preserve the unfinished offline experiment captured
before workspace cleanup. They are not the v91 baseline and have not passed
candidate qualification, signing or hardware testing.

The experiment defers framebuffer access until watchdog admission, suppresses
the text-console alias and checks the proposed write-combine mapping page by
page. `source-manifest.json` identifies the exact input bytes and parent.

Do not select it as the default build or flash it merely because the sources are
present here. Continue from the current plan and review its machine-code and
memory-map evidence before a device test.
