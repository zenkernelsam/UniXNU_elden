/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "ActuatorLive.h"
#include "EldenActuatorCalibration.h"
/* A separate translation unit keeps the native lifecycle compiled even when
 * this immutable generated profile is absent. No synthetic enabled profile is
 * used by the native/full-link build, and runtime output still refuses NULL. */
extern "C" const EldenActuatorLimits *elden_actuator_profile_get(void){
    return ELDEN_HAPTICS_HAS_ACTUATOR?&elden_actuator_limits:nullptr;
}
