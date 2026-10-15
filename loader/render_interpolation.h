#ifndef SF4CE_RENDER_INTERPOLATION_H
#define SF4CE_RENDER_INTERPOLATION_H
#include <stdint.h>
#include <stddef.h>
// Draw-only interpolation. Simulation, collision and input buffers are untouched.
void RenderInterpolationBegin(bool enabled, bool simulation, float alpha);
void RenderInterpolationVertex(int size, unsigned type, int stride, const void *pointer);
void RenderInterpolationTexCoord(int size, unsigned type, int stride, const void *pointer);
void RenderInterpolationTextureChanged(unsigned texture);
void RenderInterpolationBuffer(unsigned target, unsigned buffer);
bool RenderInterpolationDraw(unsigned mode, int first, int count, unsigned index_type, const void *indices);
void RenderInterpolationRestore();
uint64_t RenderInterpolationDraws();
size_t RenderInterpolationMemory();
#endif
