/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <device_info.h>
#include <storage_settings.h>
#include <stdbool.h>
int storage_settings_native_open(enum device_board actual_board,
                                  const struct kshim_config_values *defaults);
/* Records the selected system after UI teardown, preserving incoming MMU
 * state. An unsafe controller/MMU cleanup forbids subsequent payload handoff. */
int storage_settings_native_note_boot(uint32_t target);
struct kshim_settings_session *storage_settings_native_session(void);
bool storage_settings_native_handoff_safe(void);
int storage_settings_native_export(void *fdt);
/* BCB fallback path for recovery/bootloader when the SPMI restart_reason
 * channel is foreign-EE owned: RMW the 32-byte BCB command in misc (LUN0). */
int storage_settings_native_write_bcb(const char *command);
int storage_settings_native_export_advlog(void);
