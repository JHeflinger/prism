#ifndef TEXTURE_H
#define TEXTURE_H

#include "renderer/rstructs.h"
#include <stdint.h>

#define MAX_TEXTURE_NAME_SIZE 256
#define MAX_TEXTURE_PATH_SIZE 1024
#define MAX_TEXTURE_LEVELS 16
#define MAX_TEXTURE_DIMENSION 16384

#define TEXTURE_RECORD_STRIDE (8 + MAX_TEXTURE_LEVELS)
#define TEXTURE_FLAG_HDR 0x1u
#define TEXTURE_FLAG_SRGB 0x2u
#define TEXTURE_WRAP_SHIFT 2u
#define TEXTURE_FLAG_NORMAL 0x10u
#define TEXTURE_FLAG_FLIPGREEN 0x20u
#define TEXTURE_IMPORTANCE_MAX_W 512
#define TEXTURE_IMPORTANCE_MAX_H 256

typedef enum {
    TEXWRAP_REPEAT = 0,
    TEXWRAP_CLAMP,
    TEXWRAP_MIRROR,
    TEXWRAP_COUNT
} TexWrapMode;

typedef enum {
    MIPFILTER_BOX = 0,
    MIPFILTER_TENT,
    MIPFILTER_GAUSSIAN,
    MIPFILTER_MITCHELL,
    MIPFILTER_LANCZOS,
    MIPFILTER_KAISER,
    MIPFILTER_COUNT
} MipFilter;

typedef struct {
    uint32_t width;
    uint32_t height;
    void* data; // width * height RGBA, uint8_t for LDR textures, float for HDR textures
} TextureLevel;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t levels;
    BOOL hdr;
    BOOL srgb;
    BOOL normalmap;
    BOOL linearmips;
    uint32_t wrap;
    uint32_t mipfilter;
    uint32_t maxlevels;
    float sharpness;
    BOOL flipgreen;
} TextureSettings;

typedef struct {
    TextureSettings settings;
    TextureLevel mips[MAX_TEXTURE_LEVELS];
    char source[MAX_TEXTURE_PATH_SIZE];
    Texture2D preview;
    uint32_t previewlevel;
    BOOL previewvalid;
} SceneTexture;

size_t NumTextures();

SceneTexture* TextureReference(size_t index);

char* TextureName(size_t index);

char** TextureNameReference(size_t index);

char** TextureHandleLabels();

TextureHandle TextureHandleOf(size_t index);

size_t SubmitNamedTexture(SceneTexture texture, const char* name);

size_t LoadTextureFile(const char* filepath, BOOL reuse);

BOOL DeleteTexture(size_t index);

void ClearTextures();

void UpdateTextures();

BOOL GenerateTextureMipmaps(size_t index);

void ClearTextureMipmaps(size_t index);

size_t TextureBytes(size_t index);

void TextureTexel(const SceneTexture* tex, uint32_t level, uint32_t x, uint32_t y, BOOL decode, float out[4]);

Texture2D* TexturePreview(size_t index, uint32_t level);

void FreeSceneTexture(SceneTexture* texture);

BOOL TexturesNeedUpdate();

void PackTextures();

void ResolveTextureUpdate();

char** MipFilterLabels();

char** TextureWrapLabels();

char** TextureFilterLabels();

char** LightModeLabels();

char** MaterialTextureSlotLabels();

#endif
