/* SPDX-License-Identifier: MIT */
#pragma once
#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>
/* Returns an owned, real raster image. Extra CI shape/content effects are not
 * implemented here; retain the original already-rendered background and glyph. */
CGImageRef elden_graphic_variant_cpu(CGImageRef background,CGImageRef symbol,
    CGRect destination,CGFloat sourceScale,CGFloat outputScale,
    CGColorRef foreground,bool disableColorFill,int *error);
