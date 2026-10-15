#include "../loader/render_interpolation.h"
#include "gl_stub/vitaGL.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>
static int mode = GL_MODELVIEW, texture = 7;
static bool uv_enabled = true;
unsigned char glIsEnabled(unsigned what) { return what == GL_VERTEX_ARRAY || uv_enabled; }
static float model[16], projection[16];
static const float *draw_vertices;
void glGetIntegerv(unsigned what, int *out) { *out = what == GL_MATRIX_MODE ? mode : texture; }
void glGetFloatv(unsigned what, float *out) { memcpy(out, what == GL_MODELVIEW_MATRIX ? model : projection, 64); }
void glMatrixMode(unsigned what) { mode = what; }
void glLoadMatrixf(const float *in) { memcpy(mode == GL_MODELVIEW ? model : projection, in, 64); }
void glVertexPointer(int, unsigned, int, const void *in) { draw_vertices = static_cast<const float *>(in); }
int main() {
    for (int i = 0; i < 16; i += 5) model[i] = projection[i] = 1;
    float vertices[] = {0, 0, 0, 10, 0, 0, 0, 10, 0};
    uint16_t indices[] = {0, 1, 2};
    float uv[] = {0, 0, 1, 0, 0, 1};
    RenderInterpolationTexCoord(2, GL_FLOAT, 0, uv);
    RenderInterpolationVertex(3, GL_FLOAT, 0, vertices);
    RenderInterpolationBegin(true, true, 0);
    assert(!RenderInterpolationDraw(4, 0, 3, GL_UNSIGNED_SHORT, indices));
    for (int i = 0; i < 9; ++i) vertices[i] += 3;
    model[12] = 6;
    RenderInterpolationBegin(true, true, 0);
    assert(RenderInterpolationDraw(4, 0, 3, GL_UNSIGNED_SHORT, indices));
    assert(draw_vertices[0] == 0 && model[12] == 0);
    RenderInterpolationRestore(); assert(draw_vertices == vertices && model[12] == 6);
    RenderInterpolationBegin(true, false, 1.0f / 3);
    assert(RenderInterpolationDraw(4, 0, 3, GL_UNSIGNED_SHORT, indices));
    assert(fabsf(draw_vertices[0] - 1) < 1e-6 && fabsf(model[12] - 2) < 1e-6);
    RenderInterpolationRestore(); assert(model[12] == 6 && mode == GL_MODELVIEW);
    RenderInterpolationBegin(true, false, 2.0f / 3);
    assert(RenderInterpolationDraw(4, 0, 3, GL_UNSIGNED_SHORT, indices));
    assert(fabsf(draw_vertices[0] - 2) < 1e-6 && fabsf(model[12] - 4) < 1e-6);
    RenderInterpolationRestore();
    RenderInterpolationBegin(true, false, .5f); texture = 8;
    assert(!RenderInterpolationDraw(4, 0, 3, GL_UNSIGNED_SHORT, indices)); // topology/material change
    RenderInterpolationBuffer(GL_ARRAY_BUFFER, 1);
    RenderInterpolationVertex(3, GL_FLOAT, 0, vertices);
    RenderInterpolationBegin(true, true, 0);
    assert(!RenderInterpolationDraw(4, 0, 3, GL_UNSIGNED_SHORT, indices)); // never read a VBO offset
    RenderInterpolationBuffer(GL_ARRAY_BUFFER, 0);
    RenderInterpolationVertex(3, GL_FLOAT, 0, vertices);
    RenderInterpolationBegin(false, false, 0);
    RenderInterpolationBegin(true, true, 0);
    assert(!RenderInterpolationDraw(4, 0, 3, GL_UNSIGNED_SHORT, indices)); // cinematic transition resets history
    indices[2] = 65535;
    RenderInterpolationBegin(true, true, 0);
    assert(!RenderInterpolationDraw(4, 0, 3, GL_UNSIGNED_SHORT, indices)); // bounded index scan
    indices[2] = 2;
    RenderInterpolationBegin(true, true, 0);
    assert(!RenderInterpolationDraw(4, 0, 3, GL_UNSIGNED_SHORT, indices));
    uv[0] = .5f;
    RenderInterpolationBegin(true, true, 0);
    assert(!RenderInterpolationDraw(4, 0, 3, GL_UNSIGNED_SHORT, indices)); // an atlas/UV change resets history
    RenderInterpolationBegin(true, true, 0);
    assert(RenderInterpolationDraw(4, 0, 3, GL_UNSIGNED_SHORT, indices));
    RenderInterpolationRestore();
    RenderInterpolationTextureChanged(texture);
    RenderInterpolationBegin(true, true, 0);
    assert(!RenderInterpolationDraw(4, 0, 3, GL_UNSIGNED_SHORT, indices)); // texture contents changed at the same ID
    indices[1] = 2; indices[2] = 1;
    RenderInterpolationBegin(true, true, 0);
    assert(!RenderInterpolationDraw(4, 0, 3, GL_UNSIGNED_SHORT, indices)); // mutable indices at the same address
    assert(RenderInterpolationDraws() == 4);
    RenderInterpolationBegin(false, false, 0);
    std::vector<float> large(16384 * 3);
    uint16_t wide_indices[] = {0, 16383, 1};
    RenderInterpolationVertex(3, GL_FLOAT, 0, large.data());
    RenderInterpolationTexCoord(0, 0, 0, nullptr);
    uv_enabled = false; // unused stale UV pointers must not be read
    RenderInterpolationBegin(true, true, 0);
    for (unsigned i = 0; i < 128; ++i)
        assert(!RenderInterpolationDraw(4, 0, 3, GL_UNSIGNED_SHORT, wide_indices));
    assert(RenderInterpolationMemory() <= 4 * 1024 * 1024);
    assert(RenderInterpolationMemory() > 3 * 1024 * 1024); // exercised the real allocation budget
    assert(!RenderInterpolationDraw(4, 0, 70000, GL_UNSIGNED_SHORT, wide_indices));
    puts("Render interpolation: distinct intermediate geometry, matrices restored, topology/VBO/cut/bounds guards OK");
}
