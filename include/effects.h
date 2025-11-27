#ifndef EFFECTS_H
#define EFFECTS_H

#include "RenderContext.h"

// Effect 0: Per-Arm Blobs
void setupPerArmBlobs();
void renderPerArmBlobs(const RenderContext& ctx);

// Effect 1: Virtual Display Blobs
void setupVirtualBlobs();
void renderVirtualBlobs(const RenderContext& ctx);

// Effect 2: Solid Arms Diagnostic
void renderSolidArms(const RenderContext& ctx);

// Effect 3: RPM Arc
void renderRpmArc(const RenderContext& ctx);

#endif // EFFECTS_H
