// SPDX-License-Identifier: BSD-2-Clause-Patent
// Generated after exact original-image qualification, never a copied live vtable.
#pragma once
#include <IOKit/IOService.h>
class AppleARMBacklight : public IOService {
 OSDeclareAbstractStructors(AppleARMBacklight)
 unsigned char originalPrivate[0x370-sizeof(IOService)];
public:
 virtual void unqualifiedBacklightMethod()=0;
};
static_assert(sizeof(AppleARMBacklight)==0x370,"Original instance extent");
