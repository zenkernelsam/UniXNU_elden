# IconServices CPU path preparation

The exact iPadOS 17.7.11/21H461 IconServices image contains non-CoreImage drawing branches. ISCompositor's default mode 1 enables CoreImage effects; mode 0 makes both canUseCoreImage and canUseCoreImageForEffects return false. The original drawLayer method has CGImage rendering paths. This is a prospective workaround for the observed IconServices GL-context crash and Preferences icon wait, not a general software CoreImage renderer.

package/EldenIconCpuMode.c is an undeployed userspace experiment. Explicit ELDEN_ICON_CPU_MODE=0, the iconservicesagent process, exact framework UUID, original getter address/bytes and ObjC signature are required before replacing the renderingMode getter. It does not modify the original Apple library on disk. It compiled for arm64/iOS17 and passed ad-hoc signature verification. No runtime result is claimed.

The original job runs /System/Library/CoreServices/iconservicesagent as _iconservices and publishes com.apple.iconservices. The paired workbench prepared a derivative preserving the job's identity and policy fields while adding scoped environment variables and console stderr. RAM-only launch configuration integration and dyld/trust handling are still pending; this is not a flash candidate. Keep IconServices enabled and retain GPU-last ordering.

The ISRenderingMode string in a GraphicSymbol recipe describes a separate style enum. It is not evidence of a global compositor preference. Do not set invented environment switches or treat successful compilation as functional rendering validation. The next combined test must observe installation, cessation of the specific GL fault, Settings content and recovery; real physical touch remains a separate user acceptance check.
