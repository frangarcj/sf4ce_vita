#include "render_interpolation.h"
#include "frame_timing.h"
#include <vitaGL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr unsigned kSlots = 128;
constexpr unsigned kMaxVertices = 16384;
constexpr size_t kBudget = 4 * 1024 * 1024;
struct Slot {
    const void *vertex = nullptr, *indices = nullptr;
    unsigned texture = 0, mode = 0, count = 0, vertices = 0, epoch = 0;
    uint32_t topology = 0, uv_stamp = 0;
    float *memory = nullptr;
    size_t capacity = 0;
    float model[16] = {}, previous_model[16] = {};
    float projection[16] = {}, previous_projection[16] = {};
    bool history = false;
};
Slot slots[kSlots];
unsigned epoch = 0, draw_slot = 0, array_buffer = 0, element_buffer = 0;
unsigned vertex_buffer = 0, uv_buffer = 0;
size_t allocated = 0;
uint64_t interpolated = 0;
bool active = false, capture = false, applied = false;
float alpha = 1;
int vertex_size = 0, vertex_stride = 0, original_mode = GL_MODELVIEW;
unsigned vertex_type = 0;
const void *vertex_pointer = nullptr, *uv_pointer = nullptr;
int uv_size = 0, uv_stride = 0;
unsigned uv_type = 0;
float saved_model[16], saved_projection[16];
}
void RenderInterpolationBegin(bool enabled, bool simulation, float phase) {
    if (!enabled) {
        if (active) for (auto &slot : slots) slot.epoch = 0;
        active = false; return;
    }
    active = true; capture = simulation; alpha = phase; draw_slot = 0;
    if (simulation && !++epoch) ++epoch;
}
void RenderInterpolationVertex(int size, unsigned type, int stride, const void *pointer) {
    vertex_size = size; vertex_type = type; vertex_stride = stride; vertex_pointer = pointer;
    vertex_buffer = array_buffer;
}
void RenderInterpolationTexCoord(int size, unsigned type, int stride, const void *pointer) {
    uv_size = size; uv_type = type; uv_stride = stride; uv_pointer = pointer;
    uv_buffer = array_buffer;
}
void RenderInterpolationTextureChanged(unsigned texture) {
    for (auto &slot : slots) if (slot.texture == texture) {
        slot.epoch = 0; slot.history = false;
    }
}
void RenderInterpolationBuffer(unsigned target, unsigned buffer) {
    if (target == GL_ARRAY_BUFFER) array_buffer = buffer;
    if (target == GL_ELEMENT_ARRAY_BUFFER) element_buffer = buffer;
}
bool RenderInterpolationDraw(unsigned mode, int first, int count, unsigned index_type, const void *indices) {
    if (!active || array_buffer || vertex_buffer || element_buffer || !glIsEnabled(GL_VERTEX_ARRAY) || vertex_size != 3 ||
        vertex_type != GL_FLOAT || !vertex_pointer || first < 0 || count <= 0 || count > 65536)
        return false;
    if (draw_slot >= kSlots) return false;
    Slot &slot = slots[draw_slot++];
    unsigned vertices = 0;
    uint32_t topology = 2166136261u ^ index_type;
    if (!index_type) {
        if (first > static_cast<int>(kMaxVertices) || count > static_cast<int>(kMaxVertices) - first) return false;
        vertices = first + count;
    } else {
        if (!indices || (index_type != GL_UNSIGNED_SHORT && index_type != GL_UNSIGNED_BYTE)) return false;
        for (int i = 0; i < count; ++i) {
            unsigned v;
            if (index_type == GL_UNSIGNED_SHORT) { uint16_t s; memcpy(&s, static_cast<const char *>(indices) + i * 2, 2); v = s; }
            else v = static_cast<const uint8_t *>(indices)[i];
            topology = (topology ^ v) * 16777619u;
            if (v >= kMaxVertices) return false;
            if (v + 1 > vertices) vertices = v + 1;
        }
    }
    int stride = vertex_stride ? vertex_stride : 3 * sizeof(float);
    if (stride < 12 || stride > 256) return false;
    int texture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    uint32_t uv_stamp = 0;
    if (glIsEnabled(GL_TEXTURE_COORD_ARRAY)) {
        if (uv_buffer || !uv_pointer || uv_size != 2 || uv_type != GL_FLOAT) return false;
        int step = uv_stride ? uv_stride : 8;
        if (step < 8 || step > 256) return false;
        uv_stamp = 2166136261u;
        for (unsigned v = 0; v < vertices; ++v) {
            uint32_t bits[2];
            memcpy(bits, static_cast<const char *>(uv_pointer) + v * step, 8);
            uv_stamp = ((uv_stamp ^ bits[0]) * 16777619u ^ bits[1]) * 16777619u;
        }
    }
    bool same = slot.vertex == vertex_pointer && slot.indices == indices &&
        slot.texture == static_cast<unsigned>(texture) && slot.mode == mode &&
        slot.count == static_cast<unsigned>(count) && slot.vertices == vertices && slot.topology == topology && slot.uv_stamp == uv_stamp;
    if (capture) {
        size_t values = vertices * 3;
        if (values > slot.capacity) {
            size_t bytes = values * sizeof(float) * 3;
            size_t old = slot.capacity * sizeof(float) * 3;
            if (allocated - old + bytes > kBudget) { slot.epoch = 0; return false; }
            float *memory = static_cast<float *>(realloc(slot.memory, bytes));
            if (!memory) { slot.epoch = 0; return false; }
            slot.memory = memory; allocated = allocated - old + bytes; slot.capacity = values;
            same = false;
        }
        float *current = slot.memory, *previous = slot.memory + slot.capacity;
        slot.history = same && slot.epoch && slot.epoch + 1 == epoch;
        if (slot.history) {
            memcpy(previous, current, values * sizeof(float));
            memcpy(slot.previous_model, slot.model, sizeof(slot.model));
            memcpy(slot.previous_projection, slot.projection, sizeof(slot.projection));
        }
        for (unsigned v = 0; v < vertices; ++v)
            memcpy(current + v * 3, static_cast<const char *>(vertex_pointer) + v * stride, 12);
        glGetFloatv(GL_MODELVIEW_MATRIX, slot.model);
        glGetFloatv(GL_PROJECTION_MATRIX, slot.projection);
        // Snap topology changes, discontinuous turns and camera cuts.
        if (slot.history) {
            float facing = 0;
            for (unsigned i = 0; i < 3; ++i) facing += slot.model[i] * slot.previous_model[i];
            if (facing <= 0) slot.history = false;
            for (unsigned i = 12; i < 15; ++i)
                if (fabsf(slot.model[i] - slot.previous_model[i]) > 128) slot.history = false;
        }
        slot.vertex = vertex_pointer; slot.indices = indices; slot.texture = texture;
        slot.mode = mode; slot.count = count; slot.vertices = vertices; slot.epoch = epoch; slot.topology = topology; slot.uv_stamp = uv_stamp;
    } else if (!same || slot.epoch != epoch) return false;
    if (!slot.history) return false;
    float *mixed = slot.memory + slot.capacity * 2;
    for (unsigned i = 0; i < vertices * 3; ++i) {
        float a = slot.memory[slot.capacity + i], b = slot.memory[i];
        if (!isfinite(a) || !isfinite(b)) return false;
        mixed[i] = frame_timing::Blend(a, b, alpha);
    }
    float model[16], projection[16];
    for (unsigned i = 0; i < 16; ++i) {
        if (!isfinite(slot.previous_model[i]) || !isfinite(slot.model[i]) ||
            !isfinite(slot.previous_projection[i]) || !isfinite(slot.projection[i])) return false;
        model[i] = frame_timing::Blend(slot.previous_model[i], slot.model[i], alpha);
        projection[i] = frame_timing::Blend(slot.previous_projection[i], slot.projection[i], alpha);
    }
    glGetIntegerv(GL_MATRIX_MODE, &original_mode);
    glGetFloatv(GL_MODELVIEW_MATRIX, saved_model);
    glGetFloatv(GL_PROJECTION_MATRIX, saved_projection);
    glMatrixMode(GL_MODELVIEW); glLoadMatrixf(model);
    glMatrixMode(GL_PROJECTION); glLoadMatrixf(projection);
    glMatrixMode(original_mode);
    glVertexPointer(3, GL_FLOAT, 0, mixed);
    applied = true; ++interpolated;
    return true;
}
void RenderInterpolationRestore() {
    if (!applied) return;
    glMatrixMode(GL_MODELVIEW); glLoadMatrixf(saved_model);
    glMatrixMode(GL_PROJECTION); glLoadMatrixf(saved_projection);
    glMatrixMode(original_mode);
    glVertexPointer(vertex_size, vertex_type, vertex_stride, vertex_pointer);
    applied = false;
}
uint64_t RenderInterpolationDraws() { return interpolated; }

size_t RenderInterpolationMemory() { return allocated; }
