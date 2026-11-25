#include "texture.h"
#include "renderer/renderer.h"
#include "ui/panels/edit.h"
#include <util/logger.h>
#include <easymemory.h>
#include <easyfile.h>
#include <math.h>

static void* TexRealloc(void* ptr, size_t size) {
    if (ptr == NULL) return EZ_ALLOC(size, 1);
    return EZ_REALLOC(ptr, size, 1);
}

#define STBI_MALLOC(sz) EZ_ALLOC(sz, 1)
#define STBI_REALLOC(p, newsz) TexRealloc(p, newsz)
#define STBI_FREE(p) EZ_FREE(p)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wshadow"
#include <stb_image.h>
#pragma GCC diagnostic pop

static SceneTexture* g_textures = NULL;
static size_t g_texture_count = 0;
static size_t g_texture_capacity = 0;
static char** g_texture_names = NULL; // [0] = "None", [i + 1] = texture i
static char g_none_label[] = "None";
static BOOL g_textures_dirty = FALSE;

static char* g_mipfilter_labels[] = { "box", "tent", "gaussian", "mitchell", "lanczos", "kaiser" };
static char* g_wrap_labels[] = { "repeat", "clamp", "mirror" };
static char* g_filter_labels[] = { "bilinear", "trilinear", "anisotropic", "nearest" };
static char* g_lightmode_labels[] = { "standard", "projector", "environment" };
static char* g_slot_labels[] = {
    "Emission", "Ambient", "Diffuse", "Specular", "Absorbtion", "Dispersion", "Scattering",
    "Anisotropy", "Transport Scale", "Subsurface Ratio", "Index of Refraction", "Shininess", "Normal Map" };

static float SRGBToLinear(float c) {
    if (c <= 0.04045f) return c / 12.92f;
    return powf((c + 0.055f) / 1.055f, 2.4f);
}

static float LinearToSRGB(float c) {
    if (c <= 0.0f) return 0.0f;
    if (c <= 0.0031308f) return c * 12.92f;
    return 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

static uint16_t FloatToHalf(float f) {
    uint32_t x;
    memcpy(&x, &f, sizeof(x));
    uint32_t sign = (x >> 16) & 0x8000u;
    int32_t exponent = (int32_t)((x >> 23) & 0xFFu) - 127 + 15;
    uint32_t mantissa = x & 0x7FFFFFu;
    if (((x >> 23) & 0xFFu) == 0xFFu) return (uint16_t)(sign | 0x7C00u | (mantissa ? 0x200u : 0u));
    if (exponent >= 31) return (uint16_t)(sign | 0x7C00u);
    if (exponent <= 0) {
        if (exponent < -10) return (uint16_t)sign;
        mantissa |= 0x800000u;
        uint32_t shift = (uint32_t)(14 - exponent);
        uint32_t half = mantissa >> shift;
        if ((mantissa >> (shift - 1)) & 1u) half++;
        return (uint16_t)(sign | half);
    }
    uint32_t half = sign | ((uint32_t)exponent << 10) | (mantissa >> 13);
    if (mantissa & 0x1000u) half++;
    return (uint16_t)half;
}

static int WrapIndex(int i, int n, uint32_t wrap) {
    if (n <= 1) return 0;
    if (wrap == TEXWRAP_CLAMP) return i < 0 ? 0 : (i >= n ? n - 1 : i);
    if (wrap == TEXWRAP_MIRROR) {
        int period = 2 * n;
        int p = i % period;
        if (p < 0) p += period;
        return p < n ? p : period - 1 - p;
    }
    int p = i % n;
    return p < 0 ? p + n : p;
}

static void EnsureCapacity(size_t count) {
    if (count <= g_texture_capacity) return;
    size_t cap = g_texture_capacity == 0 ? 8 : g_texture_capacity * 2;
    while (cap < count) cap *= 2;
    if (g_textures == NULL) {
        g_textures = EZ_ALLOC(cap, sizeof(SceneTexture));
        g_texture_names = EZ_ALLOC(cap + 1, sizeof(char*));
    } else {
        g_textures = EZ_REALLOC(g_textures, cap, sizeof(SceneTexture));
        g_texture_names = EZ_REALLOC(g_texture_names, cap + 1, sizeof(char*));
    }
    g_texture_capacity = cap;
    g_texture_names[0] = g_none_label;
}

static void FreeLevels(SceneTexture* tex, uint32_t first) {
    for (uint32_t l = first; l < MAX_TEXTURE_LEVELS; l++) {
        if (tex->mips[l].data) EZ_FREE(tex->mips[l].data);
        tex->mips[l] = (TextureLevel){ 0 };
    }
    if (tex->settings.levels > first) tex->settings.levels = first > 0 ? first : 1;
}

static void InvalidatePreview(SceneTexture* tex) {
    if (tex->previewvalid) UnloadTexture(tex->preview);
    tex->preview = (Texture2D){ 0 };
    tex->previewvalid = FALSE;
}

size_t NumTextures() {
    return g_texture_count;
}

SceneTexture* TextureReference(size_t index) {
    EZ_ASSERT(index < g_texture_count, "Invalid texture index requested");
    return &(g_textures[index]);
}

char* TextureName(size_t index) {
    EZ_ASSERT(index < g_texture_count, "Invalid texture index requested");
    return g_texture_names[index + 1];
}

char** TextureNameReference(size_t index) {
    EnsureCapacity(1);
    return &(g_texture_names[index + 1]);
}

char** TextureHandleLabels() {
    EnsureCapacity(1);
    return g_texture_names;
}

TextureHandle TextureHandleOf(size_t index) {
    return index < g_texture_count ? (TextureHandle)(index + 1) : 0;
}

void FreeSceneTexture(SceneTexture* texture) {
    FreeLevels(texture, 0);
    InvalidatePreview(texture);
}

size_t SubmitNamedTexture(SceneTexture texture, const char* name) {
    EnsureCapacity(g_texture_count + 1);
    if (texture.settings.levels == 0) texture.settings.levels = 1;
    if (texture.settings.sharpness <= 0.0f) texture.settings.sharpness = 1.0f;
    texture.preview = (Texture2D){ 0 };
    texture.previewvalid = FALSE;
    g_textures[g_texture_count] = texture;
    char* b = EZ_ALLOC(MAX_TEXTURE_NAME_SIZE + 1, sizeof(char));
    strncpy(b, name ? name : "Texture", MAX_TEXTURE_NAME_SIZE);
    g_texture_names[g_texture_count + 1] = b;
    g_texture_count++;
    UpdateTextures();
    return g_texture_count - 1;
}

size_t LoadTextureFile(const char* filepath, BOOL reuse) {
    if (!filepath || !filepath[0]) return (size_t)-1;
    if (reuse) {
        for (size_t i = 0; i < g_texture_count; i++)
            if (strcmp(g_textures[i].source, filepath) == 0) return i;
    }
    int w = 0, h = 0, channels = 0;
    BOOL hdr = stbi_is_hdr(filepath);
    void* pixels = hdr ? (void*)stbi_loadf(filepath, &w, &h, &channels, 4) : (void*)stbi_load(filepath, &w, &h, &channels, 4);
    if (!pixels) {
        logerror("Unable to load texture \"%s\": %s", filepath, stbi_failure_reason());
        return (size_t)-1;
    }
    if (w <= 0 || h <= 0 || w > MAX_TEXTURE_DIMENSION || h > MAX_TEXTURE_DIMENSION) {
        logerror("Unable to load texture \"%s\": %dx%d is outside the supported size (1 to %d per side)", filepath, w, h, MAX_TEXTURE_DIMENSION);
        EZ_FREE(pixels);
        return (size_t)-1;
    }
    SceneTexture tex = { 0 };
    tex.settings.width = (uint32_t)w;
    tex.settings.height = (uint32_t)h;
    tex.settings.levels = 1;
    tex.settings.hdr = hdr;
    tex.settings.srgb = !hdr;
    tex.settings.linearmips = TRUE;
    tex.settings.wrap = TEXWRAP_REPEAT;
    tex.settings.mipfilter = MIPFILTER_BOX;
    tex.settings.sharpness = 1.0f;
    tex.mips[0] = (TextureLevel){ (uint32_t)w, (uint32_t)h, pixels };
    strncpy(tex.source, filepath, MAX_TEXTURE_PATH_SIZE - 1);
    const char* basename = ez_strip_filename(filepath);
    size_t index = SubmitNamedTexture(tex, basename ? basename : filepath);
    loginfo("Loaded %s texture \"%s\" (%dx%d)", hdr ? "HDR" : "LDR", filepath, w, h);
    return index;
}

BOOL DeleteTexture(size_t index) {
    if (index >= g_texture_count) return FALSE;
    TextureHandle handle = (TextureHandle)(index + 1);
    FreeSceneTexture(&(g_textures[index]));
    EZ_FREE(g_texture_names[index + 1]);
    for (size_t i = index; i + 1 < g_texture_count; i++) {
        g_textures[i] = g_textures[i + 1];
        g_texture_names[i + 1] = g_texture_names[i + 2];
    }
    g_texture_count--;
    g_textures[g_texture_count] = (SceneTexture){ 0 };
    g_texture_names[g_texture_count + 1] = NULL;
    for (size_t m = 0; m < NumMaterials(); m++) {
        SurfaceMaterial* mat = MaterialReference(m);
        for (size_t s = 0; s < MATERIAL_TEXTURE_SLOTS; s++) {
            if (mat->textures[s] == handle) mat->textures[s] = 0;
            else if (mat->textures[s] > handle) mat->textures[s]--;
        }
    }
    for (size_t l = 0; l < NumLights(); l++) {
        SceneLight* light = LightReference(l);
        if (light->texture == handle) light->texture = 0;
        else if (light->texture > handle) light->texture--;
    }
    FixDeleteEditTarget(EDIT_TEXTURE, index);
    UpdateMaterials();
    UpdateLights();
    UpdateTextures();
    return TRUE;
}

void ClearTextures() {
    for (size_t i = 0; i < g_texture_count; i++) {
        FreeSceneTexture(&(g_textures[i]));
        EZ_FREE(g_texture_names[i + 1]);
    }
    if (g_textures) EZ_FREE(g_textures);
    if (g_texture_names) EZ_FREE(g_texture_names);
    g_textures = NULL;
    g_texture_names = NULL;
    g_texture_count = 0;
    g_texture_capacity = 0;
    ARRLIST_uint32_t_clear(&(RendererGeometry()->textures));
    UpdateTextures();
}

void UpdateTextures() {
    g_textures_dirty = TRUE;
}

void TextureTexel(const SceneTexture* tex, uint32_t level, uint32_t x, uint32_t y, BOOL decode, float out[4]) {
    const TextureLevel* lv = &(tex->mips[level]);
    size_t i = ((size_t)y * lv->width + x) * 4;
    if (tex->settings.hdr) {
        const float* f = (const float*)lv->data;
        for (int c = 0; c < 4; c++) out[c] = f[i + c];
    } else {
        const uint8_t* b = (const uint8_t*)lv->data;
        for (int c = 0; c < 4; c++) out[c] = b[i + c] / 255.0f;
    }
    if (decode && tex->settings.srgb && !tex->settings.hdr)
        for (int c = 0; c < 3; c++) out[c] = SRGBToLinear(out[c]);
}

static float BesselI0(float x) {
    float sum = 1.0f, term = 1.0f, q = x * x * 0.25f;
    for (int k = 1; k < 32; k++) {
        term *= q / (float)(k * k);
        sum += term;
        if (term < sum * 1e-8f) break;
    }
    return sum;
}

static float Sinc(float x) {
    if (fabsf(x) < 1e-6f) return 1.0f;
    float px = (float)M_PI * x;
    return sinf(px) / px;
}

static float FilterRadius(uint32_t filter) {
    switch (filter) {
        case MIPFILTER_BOX: return 0.5f;
        case MIPFILTER_TENT: return 1.0f;
        case MIPFILTER_GAUSSIAN: return 1.5f;
        case MIPFILTER_MITCHELL: return 2.0f;
        case MIPFILTER_LANCZOS: return 3.0f;
        case MIPFILTER_KAISER: return 3.0f;
        default: return 0.5f;
    }
}

static float FilterWeight(uint32_t filter, float t) {
    float a = fabsf(t);
    switch (filter) {
        case MIPFILTER_BOX: return a <= 0.5f ? 1.0f : 0.0f;
        case MIPFILTER_TENT: return a < 1.0f ? 1.0f - a : 0.0f;
        case MIPFILTER_GAUSSIAN: return a < 1.5f ? expf(-2.0f * a * a) : 0.0f; // sigma = 0.5
        case MIPFILTER_MITCHELL: { // B = C = 1/3
            const float B = 1.0f / 3.0f, C = 1.0f / 3.0f;
            if (a < 1.0f) return ((12.0f - 9.0f * B - 6.0f * C) * a * a * a + (-18.0f + 12.0f * B + 6.0f * C) * a * a + (6.0f - 2.0f * B)) / 6.0f;
            if (a < 2.0f) return ((-B - 6.0f * C) * a * a * a + (6.0f * B + 30.0f * C) * a * a + (-12.0f * B - 48.0f * C) * a + (8.0f * B + 24.0f * C)) / 6.0f;
            return 0.0f;
        }
        case MIPFILTER_LANCZOS: return a < 3.0f ? Sinc(a) * Sinc(a / 3.0f) : 0.0f;
        case MIPFILTER_KAISER: { // alpha = 4, width = 3
            const float alpha = 4.0f, width = 3.0f;
            if (a >= width) return 0.0f;
            float r = a / width;
            return Sinc(a) * BesselI0(alpha * sqrtf(1.0f - r * r)) / BesselI0(alpha);
        }
        default: return a <= 0.5f ? 1.0f : 0.0f;
    }
}

typedef struct {
    int first;
    int count;
    float* weights;
} ResampleTap;

static ResampleTap* BuildTaps(uint32_t src, uint32_t dst, uint32_t filter, float sharpness, int* maxtaps) {
    ResampleTap* taps = EZ_ALLOC(dst, sizeof(ResampleTap));
    float scale = (float)src / (float)dst;
    float radius = FilterRadius(filter) / sharpness * (scale > 1.0f ? scale : 1.0f);
    *maxtaps = 0;
    for (uint32_t x = 0; x < dst; x++) {
        float center = ((float)x + 0.5f) * scale;
        int first = (int)floorf(center - radius - 0.5f);
        int last = (int)ceilf(center + radius - 0.5f);
        int count = last - first + 1;
        taps[x].first = first;
        taps[x].count = count;
        taps[x].weights = EZ_ALLOC(count, sizeof(float));
        float total = 0.0f;
        for (int k = 0; k < count; k++) {
            float t = (((float)(first + k) + 0.5f) - center) / (scale > 1.0f ? scale : 1.0f) * sharpness;
            float w = FilterWeight(filter, t);
            taps[x].weights[k] = w;
            total += w;
        }
        if (fabsf(total) < 1e-8f) { // filter too narrow to reach a sample, fall back to the nearest one
            for (int k = 0; k < count; k++) taps[x].weights[k] = 0.0f;
            int nearest = (int)floorf(center) - first;
            if (nearest < 0) nearest = 0;
            if (nearest >= count) nearest = count - 1;
            taps[x].weights[nearest] = 1.0f;
            total = 1.0f;
        }
        for (int k = 0; k < count; k++) taps[x].weights[k] /= total;
        if (count > *maxtaps) *maxtaps = count;
    }
    return taps;
}

static void FreeTaps(ResampleTap* taps, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) EZ_FREE(taps[i].weights);
    EZ_FREE(taps);
}

static float* ResampleLevel(const float* src, uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh, const TextureSettings* s) {
    int tx = 0, ty = 0;
    float sharp = s->sharpness > 0.0f ? s->sharpness : 1.0f;
    ResampleTap* xt = BuildTaps(sw, dw, s->mipfilter, sharp, &tx);
    ResampleTap* yt = BuildTaps(sh, dh, s->mipfilter, sharp, &ty);
    float* tmp = EZ_ALLOC((size_t)dw * sh * 4, sizeof(float));
    for (uint32_t y = 0; y < sh; y++) {
        const float* row = src + (size_t)y * sw * 4;
        float* out = tmp + (size_t)y * dw * 4;
        for (uint32_t x = 0; x < dw; x++) {
            float acc[4] = { 0 };
            for (int k = 0; k < xt[x].count; k++) {
                int sx = WrapIndex(xt[x].first + k, (int)sw, s->wrap);
                float w = xt[x].weights[k];
                for (int c = 0; c < 4; c++) acc[c] += w * row[(size_t)sx * 4 + c];
            }
            for (int c = 0; c < 4; c++) out[(size_t)x * 4 + c] = acc[c];
        }
    }
    float* dst = EZ_ALLOC((size_t)dw * dh * 4, sizeof(float));
    for (uint32_t y = 0; y < dh; y++) {
        float* out = dst + (size_t)y * dw * 4;
        for (int k = 0; k < yt[y].count; k++) {
            int sy = WrapIndex(yt[y].first + k, (int)sh, s->wrap);
            float w = yt[y].weights[k];
            const float* row = tmp + (size_t)sy * dw * 4;
            for (size_t i = 0; i < (size_t)dw * 4; i++) out[i] += w * row[i];
        }
    }
    EZ_FREE(tmp);
    FreeTaps(xt, dw);
    FreeTaps(yt, dh);
    return dst;
}

static float* LevelToLinear(const SceneTexture* tex, uint32_t level) {
    const TextureLevel* lv = &(tex->mips[level]);
    size_t n = (size_t)lv->width * lv->height;
    float* out = EZ_ALLOC(n * 4, sizeof(float));
    BOOL decode = tex->settings.srgb && tex->settings.linearmips && !tex->settings.normalmap;
    for (uint32_t y = 0; y < lv->height; y++)
        for (uint32_t x = 0; x < lv->width; x++)
            TextureTexel(tex, level, x, y, decode, &out[((size_t)y * lv->width + x) * 4]);
    return out;
}

static void* LinearToLevel(const SceneTexture* tex, float* data, uint32_t w, uint32_t h) {
    size_t n = (size_t)w * h;
    BOOL encode = tex->settings.srgb && tex->settings.linearmips && !tex->settings.normalmap;
    if (tex->settings.normalmap) {
        for (size_t i = 0; i < n; i++) {
            float* p = &data[i * 4];
            float v[3] = { p[0] * 2.0f - 1.0f, p[1] * 2.0f - 1.0f, p[2] * 2.0f - 1.0f };
            float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (len < 1e-6f) { v[0] = 0.0f; v[1] = 0.0f; v[2] = 1.0f; len = 1.0f; }
            for (int c = 0; c < 3; c++) p[c] = (v[c] / len) * 0.5f + 0.5f;
        }
    }
    if (tex->settings.hdr) {
        for (size_t i = 0; i < n * 4; i++) if (data[i] < 0.0f) data[i] = 0.0f;
        return data;
    }
    uint8_t* out = EZ_ALLOC(n * 4, sizeof(uint8_t));
    for (size_t i = 0; i < n; i++) {
        for (int c = 0; c < 4; c++) {
            float v = data[i * 4 + c];
            if (encode && c < 3) v = LinearToSRGB(v);
            v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
            out[i * 4 + c] = (uint8_t)(v * 255.0f + 0.5f);
        }
    }
    EZ_FREE(data);
    return out;
}

BOOL GenerateTextureMipmaps(size_t index) {
    if (index >= g_texture_count) return FALSE;
    SceneTexture* tex = &(g_textures[index]);
    FreeLevels(tex, 1);
    uint32_t limit = tex->settings.maxlevels == 0 || tex->settings.maxlevels > MAX_TEXTURE_LEVELS ? MAX_TEXTURE_LEVELS : tex->settings.maxlevels;
    uint32_t levels = 1;
    float* current = LevelToLinear(tex, 0);
    uint32_t w = tex->settings.width, h = tex->settings.height;
    while (levels < limit && (w > 1 || h > 1)) {
        uint32_t nw = w > 1 ? w / 2 : 1;
        uint32_t nh = h > 1 ? h / 2 : 1;
        float* next = ResampleLevel(current, w, h, nw, nh, &(tex->settings));
        EZ_FREE(current);
        size_t bytes = (size_t)nw * nh * 4 * (tex->settings.hdr ? sizeof(float) : sizeof(uint8_t));
        void* stored = LinearToLevel(tex, next, nw, nh); // takes ownership of next
        if (tex->settings.hdr) {
            current = EZ_ALLOC((size_t)nw * nh * 4, sizeof(float));
            memcpy(current, stored, bytes);
        } else {
            current = EZ_ALLOC((size_t)nw * nh * 4, sizeof(float));
            const uint8_t* b = (const uint8_t*)stored;
            BOOL decode = tex->settings.srgb && tex->settings.linearmips && !tex->settings.normalmap;
            for (size_t i = 0; i < (size_t)nw * nh * 4; i++) {
                float v = b[i] / 255.0f;
                current[i] = decode && (i % 4) < 3 ? SRGBToLinear(v) : v;
            }
        }
        tex->mips[levels] = (TextureLevel){ nw, nh, stored };
        levels++;
        w = nw;
        h = nh;
    }
    EZ_FREE(current);
    tex->settings.levels = levels;
    InvalidatePreview(tex);
    UpdateTextures();
    loginfo("Generated %u mip levels for texture \"%s\" with a %s filter", levels, TextureName(index), g_mipfilter_labels[tex->settings.mipfilter % MIPFILTER_COUNT]);
    return TRUE;
}

void ClearTextureMipmaps(size_t index) {
    if (index >= g_texture_count) return;
    FreeLevels(&(g_textures[index]), 1);
    g_textures[index].settings.levels = 1;
    InvalidatePreview(&(g_textures[index]));
    UpdateTextures();
}

static size_t TextureTexelWords(const SceneTexture* tex) {
    size_t words = 0;
    for (uint32_t l = 0; l < tex->settings.levels; l++)
        words += (size_t)tex->mips[l].width * tex->mips[l].height * (tex->settings.hdr ? 2 : 1);
    return words;
}

size_t TextureBytes(size_t index) {
    if (index >= g_texture_count) return 0;
    return (TextureTexelWords(&(g_textures[index])) + TEXTURE_RECORD_STRIDE) * sizeof(uint32_t);
}

Texture2D* TexturePreview(size_t index, uint32_t level) {
    if (index >= g_texture_count) return NULL;
    SceneTexture* tex = &(g_textures[index]);
    if (level >= tex->settings.levels) level = tex->settings.levels - 1;
    if (tex->previewvalid && tex->previewlevel == level) return &(tex->preview);
    InvalidatePreview(tex);
    const TextureLevel* lv = &(tex->mips[level]);
    if (!lv->data) return NULL;
    Image img = { 0 };
    img.width = (int)lv->width;
    img.height = (int)lv->height;
    img.mipmaps = 1;
    img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    uint8_t* px = EZ_ALLOC((size_t)lv->width * lv->height * 4, sizeof(uint8_t));
    for (uint32_t y = 0; y < lv->height; y++) {
        for (uint32_t x = 0; x < lv->width; x++) {
            float c[4];
            TextureTexel(tex, level, x, y, FALSE, c);
            if (tex->settings.hdr) for (int k = 0; k < 3; k++) c[k] = LinearToSRGB(c[k] / (1.0f + c[k]));
            for (int k = 0; k < 4; k++) {
                float v = c[k] < 0.0f ? 0.0f : (c[k] > 1.0f ? 1.0f : c[k]);
                px[((size_t)y * lv->width + x) * 4 + k] = (uint8_t)(v * 255.0f + 0.5f);
            }
            px[((size_t)y * lv->width + x) * 4 + 3] = 255;
        }
    }
    img.data = px;
    tex->preview = LoadTextureFromImage(img);
    EZ_FREE(px);
    SetTextureFilter(tex->preview, TEXTURE_FILTER_POINT);
    tex->previewlevel = level;
    tex->previewvalid = TRUE;
    return &(tex->preview);
}

static BOOL TextureUsedForIBL(TextureHandle handle) {
    for (size_t l = 0; l < NumLights(); l++) {
        SceneLight* light = LightReference(l);
        if (light->mode == LIGHT_MODE_IBL && light->texture == handle) return TRUE;
    }
    return FALSE;
}

static void ImportanceSize(const SceneTexture* tex, uint32_t* gw, uint32_t* gh) {
    *gw = tex->settings.width < TEXTURE_IMPORTANCE_MAX_W ? tex->settings.width : TEXTURE_IMPORTANCE_MAX_W;
    *gh = tex->settings.height < TEXTURE_IMPORTANCE_MAX_H ? tex->settings.height : TEXTURE_IMPORTANCE_MAX_H;
    if (*gw == 0) *gw = 1;
    if (*gh == 0) *gh = 1;
}

static size_t ImportanceWords(uint32_t gw, uint32_t gh) {
    return (size_t)(gh + 1) + (size_t)gh * (gw + 1);
}

static void BuildImportance(const SceneTexture* tex, uint32_t gw, uint32_t gh, uint32_t* out) {
    float* f = EZ_ALLOC((size_t)gw * gh, sizeof(float));
    uint32_t w = tex->settings.width, h = tex->settings.height;
    for (uint32_t j = 0; j < gh; j++) {
        uint32_t y0 = (uint32_t)(((uint64_t)j * h) / gh);
        uint32_t y1 = (uint32_t)(((uint64_t)(j + 1) * h) / gh);
        if (y1 <= y0) y1 = y0 + 1;
        float theta = ((float)j + 0.5f) / (float)gh * (float)M_PI;
        float s = sinf(theta);
        for (uint32_t i = 0; i < gw; i++) {
            uint32_t x0 = (uint32_t)(((uint64_t)i * w) / gw);
            uint32_t x1 = (uint32_t)(((uint64_t)(i + 1) * w) / gw);
            if (x1 <= x0) x1 = x0 + 1;
            float sum = 0.0f;
            for (uint32_t y = y0; y < y1 && y < h; y++) {
                for (uint32_t x = x0; x < x1 && x < w; x++) {
                    float c[4];
                    TextureTexel(tex, 0, x, y, TRUE, c);
                    sum += 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
                }
            }
            float avg = sum / (float)((y1 - y0) * (x1 - x0));
            f[(size_t)j * gw + i] = (avg > 0.0f ? avg : 0.0f) * s + 1e-6f;
        }
    }
    float* marginal = (float*)out;
    float* conditional = (float*)out + (gh + 1);
    marginal[0] = 0.0f;
    for (uint32_t j = 0; j < gh; j++) {
        float* row = conditional + (size_t)j * (gw + 1);
        row[0] = 0.0f;
        for (uint32_t i = 0; i < gw; i++) row[i + 1] = row[i] + f[(size_t)j * gw + i];
        float rowsum = row[gw];
        for (uint32_t i = 1; i <= gw; i++) row[i] = rowsum > 0.0f ? row[i] / rowsum : (float)i / (float)gw;
        row[gw] = 1.0f;
        marginal[j + 1] = marginal[j] + rowsum;
    }
    float total = marginal[gh];
    for (uint32_t j = 1; j <= gh; j++) marginal[j] = total > 0.0f ? marginal[j] / total : (float)j / (float)gh;
    marginal[gh] = 1.0f;
    EZ_FREE(f);
}

BOOL TexturesNeedUpdate() {
    return g_textures_dirty;
}

void PackTextures() {
    Geometry* geo = RendererGeometry();
    size_t words = 1 + g_texture_count * TEXTURE_RECORD_STRIDE;
    for (size_t t = 0; t < g_texture_count; t++) {
        words += TextureTexelWords(&(g_textures[t]));
        if (TextureUsedForIBL((TextureHandle)(t + 1))) {
            uint32_t gw, gh;
            ImportanceSize(&(g_textures[t]), &gw, &gh);
            words += ImportanceWords(gw, gh);
        }
    }
    ARRLIST_uint32_t_clear(&(geo->textures));
    ARRLIST_uint32_t_zero(&(geo->textures), words);
    uint32_t* w = geo->textures.data;
    w[0] = (uint32_t)g_texture_count;
    size_t cursor = 1 + g_texture_count * TEXTURE_RECORD_STRIDE;
    for (size_t t = 0; t < g_texture_count; t++) {
        SceneTexture* tex = &(g_textures[t]);
        uint32_t* rec = w + 1 + t * TEXTURE_RECORD_STRIDE;
        rec[0] = tex->settings.width;
        rec[1] = tex->settings.height;
        rec[2] = tex->settings.levels;
        rec[3] = (tex->settings.hdr ? TEXTURE_FLAG_HDR : 0u) |
                 (tex->settings.srgb && !tex->settings.hdr ? TEXTURE_FLAG_SRGB : 0u) |
                 ((tex->settings.wrap % TEXWRAP_COUNT) << TEXTURE_WRAP_SHIFT) |
                 (tex->settings.normalmap ? TEXTURE_FLAG_NORMAL : 0u) |
                 (tex->settings.flipgreen ? TEXTURE_FLAG_FLIPGREEN : 0u);
        for (uint32_t l = 0; l < tex->settings.levels; l++) {
            TextureLevel* lv = &(tex->mips[l]);
            rec[8 + l] = (uint32_t)cursor;
            size_t n = (size_t)lv->width * lv->height;
            if (tex->settings.hdr) {
                const float* f = (const float*)lv->data;
                for (size_t i = 0; i < n; i++) {
                    w[cursor++] = (uint32_t)FloatToHalf(f[i * 4 + 0]) | ((uint32_t)FloatToHalf(f[i * 4 + 1]) << 16);
                    w[cursor++] = (uint32_t)FloatToHalf(f[i * 4 + 2]) | ((uint32_t)FloatToHalf(f[i * 4 + 3]) << 16);
                }
            } else {
                memcpy(w + cursor, lv->data, n * sizeof(uint32_t)); // RGBA8 bytes are already packUnorm4x8 order
                cursor += n;
            }
        }
        if (TextureUsedForIBL((TextureHandle)(t + 1))) {
            uint32_t gw, gh;
            ImportanceSize(tex, &gw, &gh);
            rec[4] = (uint32_t)cursor;
            rec[5] = gw;
            rec[6] = gh;
            BuildImportance(tex, gw, gh, w + cursor);
            cursor += ImportanceWords(gw, gh);
        }
    }
    EZ_ASSERT(cursor == words, "Texture packing size mismatch");
}

void ResolveTextureUpdate() {
    g_textures_dirty = FALSE;
}

char** MipFilterLabels() {
    return g_mipfilter_labels;
}

char** TextureWrapLabels() {
    return g_wrap_labels;
}

char** TextureFilterLabels() {
    return g_filter_labels;
}

char** LightModeLabels() {
    return g_lightmode_labels;
}

char** MaterialTextureSlotLabels() {
    return g_slot_labels;
}
