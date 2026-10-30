#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void PGCaptureNativeFrame(const char *name);
double PGRenderedFramesPerSecond(void);
void PGAttachMetalLayer(void *layer);
void PGStartGraphics(void *memory,uint64_t memorySize,uint64_t ring,uint64_t ringSize,uint64_t debug);
// The GPU samples BC textures as they are (otherwise they are decoded, about 4 times larger).
int PGGPUReadsBC(void);
// GPU bytes per budgeted byte of BC textures, in percent, from earlier sessions on this device (GTA-123), and the
// renderer's live numbers that keep it current.
int PGTextureRatioPercent(void);
void PGNoteTextureRatio(int64_t gpuBytes, int64_t gameBytes);
// Settings › Sharper upscaling: MetalFX scales the picture to the screen's resolution (changes live).
void PGSetSharperUpscaling(int on);
// Settings › MSR (Mugoat Super Resolution): MetalFX scaling to the screen plus a sharpening, glow and colour pass.
// sharpness and bloom 0...1; look 0 original, 1 natural, 2 cinematic, 3 vivid. Changes live.
void PGSetMSR(int enabled, float sharpness, float bloom, int look);
// Strict frame pacing of the renderer: 60 or 120 frames per second, changes live.
void PGSetFramePacing(int fps);
#ifdef __cplusplus
}
#endif
