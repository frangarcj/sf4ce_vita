#pragma once
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_MODELVIEW_MATRIX 0x0BA6
#define GL_PROJECTION_MATRIX 0x0BA7
#define GL_MATRIX_MODE 0x0BA0
void glGetIntegerv(unsigned, int *);
void glGetFloatv(unsigned, float *);
void glMatrixMode(unsigned);
void glLoadMatrixf(const float *);
void glVertexPointer(int, unsigned, int, const void *);

#define GL_VERTEX_ARRAY 0x8074
#define GL_TEXTURE_COORD_ARRAY 0x8078
unsigned char glIsEnabled(unsigned);
