/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "Wcn7861Startup.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {
    EldenWcnHciFirmwareError=-84,
    EldenWcnHciPrepared=14,EldenWcnHciWake,EldenWcnHciVersion,
    EldenWcnHciPatch,EldenWcnHciNvm,EldenWcnHciReset,
    EldenWcnHciPatchedVersion,EldenWcnHciLocalVersion,
    EldenWcnHciAddress,EldenWcnHciHandover,EldenWcnHciReady
};
typedef struct {
    const uint8_t *patch,*nvm;size_t patch_bytes,nvm_bytes;
    size_t baud_byte,ibs_byte,address_byte;
    uint16_t product,rom,patch_version;
    uint8_t nvm_address[6];
} EldenWcnHciFirmware;
typedef struct {
    EldenWcnStartup *wire;
    EldenWcnHciFirmware firmware;
    uint8_t address[6],local_version[8],local_address[6];
    uint16_t reported_patch;
    unsigned stage,segments,acks;
    int error;bool poisoned,ready;
} EldenWcnHci;
/* Pure complete preflight. Borrowed immutable files must remain alive.
 * The caller additionally verifies the exact TB322FC SHA manifest. */
int elden_wcn_hci_firmware_prepare(EldenWcnHciFirmware *,
    const uint8_t *patch,size_t patch_bytes,const uint8_t *nvm,size_t nvm_bytes);
/* Encode one immutable 243-byte EDL segment. Copy-only edits change NVM
 * tag17 baud, tag27 IBS and tag2 address, never tag17[0] or the source file. */
int elden_wcn_hci_segment(const EldenWcnHciFirmware *,bool nvm,size_t offset,
    const uint8_t address[6],uint8_t *packet,size_t capacity,size_t *bytes,bool *wait);
/* Takes the exact already-ready pre-HCI session, including pending RX bytes.
 * Both structures must be initially zeroed. The address must come from an
 * owner-qualified per-device source; the shared firmware default is refused.
 * No native bluetoothd service, power/clock ownership or pairing is implied. */
int elden_wcn_hci_prepare(EldenWcnHci *,EldenWcnStartup *,
    const uint8_t *patch,size_t patch_bytes,const uint8_t *nvm,size_t nvm_bytes,
    const uint8_t address[6]);
int elden_wcn_hci_run(EldenWcnHci *);
#ifdef __cplusplus
}
#endif
