#ifndef EFFECTS_H
#define EFFECTS_H

#include "RenderContext.h"

// Effect 0: Per-Arm Blobs
void setupPerArmBlobs();
void renderPerArmBlobs(RenderContext& ctx);

// Effect 1: Virtual Display Blobs
void setupVirtualBlobs();
void renderVirtualBlobs(RenderContext& ctx);

// Effect 2: Solid Arms Diagnostic
void renderSolidArms(RenderContext& ctx);

// Effect 3: RPM Arc
void renderRpmArc(RenderContext& ctx);

// Effect 4: Noise Field
void renderNoiseField(RenderContext& ctx);

#endif // EFFECTS_H
