#pragma once
#define ELDEN_ICON_ORIGINAL_PATH "/System/Library/CoreServices/iconservicesagent"
#define ELDEN_ICON_ROOT_DIR "/private/var/elden-icon-overlay"
#define ELDEN_ICON_SOURCE_DIR ELDEN_ICON_ROOT_DIR "/CoreServices"
#define ELDEN_ICON_MOUNT_DIR "/System/Library/CoreServices"
#define ELDEN_ICON_LIBRARY_PATH ELDEN_ICON_MOUNT_DIR "/EldenIconCpuMode.dylib"
#define ELDEN_ICON_PROGRAM_PATH ELDEN_ICON_ORIGINAL_PATH
#define ELDEN_ICON_LIBRARY_SOURCE ELDEN_ICON_SOURCE_DIR "/EldenIconCpuMode.dylib"
#define ELDEN_ICON_PROGRAM_SOURCE ELDEN_ICON_SOURCE_DIR "/iconservicesagent"
#define ELDEN_ICON_TRACE_CODE 0xfe700010u
#define ELDEN_ICON_TRACE_MAGIC 0x454c44454e49434fULL
