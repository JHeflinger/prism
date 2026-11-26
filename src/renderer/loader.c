#include "loader.h"
#include "renderer/renderer.h"
#include "renderer/animation.h"
#include "renderer/rig.h"
#include "renderer/rmath.h"
#include "renderer/subsurface.h"
#include "renderer/texture.h"
#include "data/defaults.h"
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <util/logger.h>
#include <easyparse.h>
#include <easyfile.h>
#include <easyhash.h>
#include <ctype.h>
#include <stddef.h>
#include <errno.h>
#include <nfd.h>

#define MAX_MTLLIB_PATH_SIZE 1024
#define MAX_OBJ_PATH_SIZE 1024
#define MAX_XML_PATH_SIZE 1024
#define MAX_OBJ_LINE_SIZE 2048
#define MAX_OBJ_ARG_SIZE 32
#define MAX_OBJ_NUM_ARGS 64
#define MAX_MTL_LINE_SIZE 2048
#define MAX_MTL_ARG_SIZE 32
#define MAX_MTL_NUM_ARGS 64
#define MAX_XML_LINE_SIZE 2048
#define MAX_XML_ARG_SIZE 32
#define MAX_XML_NUM_ARGS 64
#define MAX_PLY_HEADER_LINE 512
#define MAX_PLY_PROPERTIES 128
#define MAX_PLY_PROPERTY_NAME 64
#define PRISM_MAGIC "PRSM"
#define PRISM_VERSION 1u
#define PBJ_MAGIC "PBJ"
#define PBJ_VERSION 1u
#define PBJ_MAX_NAME 4096

#define SET_MTL_FLOAT_FIELD(field, a) state->materials.data[state->materials.size - 1].field = a;
#define SET_MTL_UINT_FIELD(field, a) state->materials.data[state->materials.size - 1].field = a;
#define SET_MTL_VEC3_FIELD(field, x, y, z) { \
    state->materials.data[state->materials.size - 1].field[0] = x; \
    state->materials.data[state->materials.size - 1].field[1] = y; \
    state->materials.data[state->materials.size - 1].field[2] = z; \
}
#define PRISM_WRITE_LIST(s, list) { \
    PrismWriteSize(s, (list).size); \
    PrismWrite(s, (list).data, (list).size * sizeof(*((list).data))); }
#define PRISM_READ_LIST(s, list) { \
    size_t _n = PrismReadSize(s); \
    (list).data = PrismReadElements(s, _n, sizeof(*((list).data))); \
    (list).size = (list).data ? _n : 0; \
    (list).maxsize = (list).size; }
#define PRISM_WRITE_MAP(s, map) { \
    PrismWriteSize(s, (map).capacity); \
    PrismWrite(s, (map).entries, (map).capacity * sizeof(*((map).entries))); }
#define PRISM_READ_MAP(s, map) { \
    size_t _cap = PrismReadSize(s); \
    if (_cap & (_cap - 1)) s->ok = FALSE; \
    (map).entries = PrismReadElements(s, _cap, sizeof(*((map).entries))); \
    (map).capacity = (map).entries ? _cap : 0; \
    if ((map).capacity == 0) { (map).size = 0; (map).tombstones = 0; } }

DECLARE_HASHMAP(size_t, size_t, Index);
IMPL_HASHMAP(size_t, size_t, Index, ez_hash_size_t);

typedef struct {
    size_t face_index;
    size_t material_ind;
} UseMaterialMarker;

typedef struct {
    float u;
    float v;
} UV;

typedef struct {
    size_t a;
    size_t b;
    size_t c;
    size_t at;
    size_t bt;
    size_t ct;
    size_t an;
    size_t bn;
    size_t cn;
    BOOL textures;
    BOOL normals;
} Face;

typedef enum {
    PLY_TYPE_INVALID = 0,
    PLY_TYPE_CHAR,
    PLY_TYPE_UCHAR,
    PLY_TYPE_SHORT,
    PLY_TYPE_USHORT,
    PLY_TYPE_INT,
    PLY_TYPE_UINT,
    PLY_TYPE_FLOAT,
    PLY_TYPE_DOUBLE
} PLYType;

typedef struct {
    char name[MAX_PLY_PROPERTY_NAME];
    PLYType type;
} PLYProperty;

typedef struct {
    PLYProperty properties[MAX_PLY_PROPERTIES];
    size_t property_count;
    size_t vertex_count;
    int64_t data_offset;
    BOOL binary_little_endian;
} PLYHeader;

DECLARE_ARRLIST(Face);
DECLARE_ARRLIST(UV);
DECLARE_ARRLIST(UseMaterialMarker);
IMPL_ARRLIST(Face);
IMPL_ARRLIST(UV);
IMPL_ARRLIST(UseMaterialMarker);
DECLARE_ARRLIST(MaterialID);
IMPL_ARRLIST(MaterialID);

typedef struct {
    ARRLIST_vec3 vertices;
    ARRLIST_vec3 normals;
    ARRLIST_UV uvs;
    ARRLIST_Face faces;
    ARRLIST_DynamicString material_names;
    ARRLIST_SurfaceMaterial materials;
    ARRLIST_UseMaterialMarker markers;
    const char* filepath;
} StateOBJ;

typedef struct {
    const char* filepath;
    VertexID vstart;
    ARRLIST_Animation animations;
} StateFBX;

typedef struct {
    FILE* file;
    BOOL ok;
    size_t remaining;
} PrismStream;

typedef struct {
    MeshDescriptor md;
    ARRLIST_Triangle triangles;
    ARRLIST_vec4 vertices;
    ARRLIST_vec3 normals;
    ARRLIST_vec2 uvs;
    ARRLIST_SurfaceMaterial materials;
    ARRLIST_DynamicString materialnames;
    ARRLIST_GaussianSplat splats;
    ARRLIST_MeshAnimation anims;
    ARRLIST_VertexSkin skins;
    ARRLIST_mat4 pose;
    uint32_t* sss;
    size_t sss_words;
} PBJMesh;

typedef struct {
    size_t count;
    SceneTexture* textures;
    char** names;
    TextureHandle* map; // local handle - 1 -> handle in the scene, filled when submitted
} PrismTextureSet;

typedef BOOL (*ParseFuncOBJ)(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t, StateOBJ*);
typedef BOOL (*ParseFuncMTL)(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t, StateOBJ*);

static void _aiv32v3(struct aiVector3D ai, vec3 out) { out[0] = ai.x; out[1] = ai.y; out[2] = ai.z; }
static void _aiq2v(struct aiQuaternion ai, versor out) { out[0] = ai.x; out[1] = ai.y; out[2] = ai.z; out[3] = ai.w; }
static void _aic2v(struct aiColor3D ai, vec3 out) { out[0] = ai.r; out[1] = ai.g; out[2] = ai.b; }
static struct aiColor3D _4d23d(struct aiColor4D c) { return (struct aiColor3D){ c.r, c.g, c.b }; }

static void CleanStateOBJ(StateOBJ* state) {
    ARRLIST_vec3_clear(&(state->vertices));
    ARRLIST_vec3_clear(&(state->normals));
    ARRLIST_UV_clear(&(state->uvs));
    ARRLIST_Face_clear(&(state->faces));
    for (size_t i = 0; i < state->material_names.size; i++)
        EZ_FREE(state->material_names.data[i]);
    ARRLIST_DynamicString_clear(&(state->material_names));
    ARRLIST_SurfaceMaterial_clear(&(state->materials));
    ARRLIST_UseMaterialMarker_clear(&(state->markers));
}

static BOOL IsWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static PLYType ParsePLYType(const char* type) {
    if (strcmp(type, "char") == 0 || strcmp(type, "int8") == 0) return PLY_TYPE_CHAR;
    if (strcmp(type, "uchar") == 0 || strcmp(type, "uint8") == 0) return PLY_TYPE_UCHAR;
    if (strcmp(type, "short") == 0 || strcmp(type, "int16") == 0) return PLY_TYPE_SHORT;
    if (strcmp(type, "ushort") == 0 || strcmp(type, "uint16") == 0) return PLY_TYPE_USHORT;
    if (strcmp(type, "int") == 0 || strcmp(type, "int32") == 0) return PLY_TYPE_INT;
    if (strcmp(type, "uint") == 0 || strcmp(type, "uint32") == 0) return PLY_TYPE_UINT;
    if (strcmp(type, "float") == 0 || strcmp(type, "float32") == 0) return PLY_TYPE_FLOAT;
    if (strcmp(type, "double") == 0 || strcmp(type, "float64") == 0) return PLY_TYPE_DOUBLE;
    return PLY_TYPE_INVALID;
}

static float ReadPLYFloat(FILE* file, PLYType type) {
    if (type == PLY_TYPE_FLOAT) {
        float value;
        if (fread(&value, sizeof(float), 1, file) != 1) return NAN;
        return value;
    }
    if (type == PLY_TYPE_DOUBLE) {
        double value;
        if (fread(&value, sizeof(double), 1, file) != 1) return NAN;
        return (float)value;
    }
    if (type == PLY_TYPE_CHAR) {
        int8_t value;
        if (fread(&value, sizeof(value), 1, file) != 1) return NAN;
        return (float)value;
    }
    if (type == PLY_TYPE_UCHAR) {
        uint8_t value;
        if (fread(&value, sizeof(value), 1, file) != 1) return NAN;
        return (float)value;
    }
    if (type == PLY_TYPE_SHORT) {
        int16_t value;
        if (fread(&value, sizeof(value), 1, file) != 1) return NAN;
        return (float)value;
    }
    if (type == PLY_TYPE_USHORT) {
        uint16_t value;
        if (fread(&value, sizeof(value), 1, file) != 1) return NAN;
        return (float)value;
    }
    if (type == PLY_TYPE_INT) {
        int32_t value;
        if (fread(&value, sizeof(value), 1, file) != 1) return NAN;
        return (float)value;
    }
    if (type == PLY_TYPE_UINT) {
        uint32_t value;
        if (fread(&value, sizeof(value), 1, file) != 1) return NAN;
        return (float)value;
    }
    return NAN;
}

static BOOL ParsePLYHeader(FILE* file, PLYHeader* header) {
    char line[MAX_PLY_HEADER_LINE];
    memset(header, 0, sizeof(*header));
    if (!fgets(line, sizeof(line), file)) return FALSE;
    if (strncmp(line, "ply", 3) != 0) {
        logerror("PLY file does not begin with \"ply\"");
        return FALSE;
    }
    BOOL found_format = FALSE;
    BOOL found_vertex = FALSE;
    BOOL in_vertex = FALSE;
    while (fgets(line, sizeof(line), file)) {
        char keyword[MAX_PLY_PROPERTY_NAME] = { 0 };
        if (sscanf(line, "%63s", keyword) != 1) continue;
        if (strcmp(keyword, "format") == 0) {
            char format[MAX_PLY_PROPERTY_NAME] = { 0 };
            float version = 0.0f;
            if (sscanf(line, "%*s %63s %f", format, &version) != 2) return FALSE;
            if (strcmp(format, "binary_little_endian") != 0) {
                logerror(
                    "Unsupported PLY format \"%s\". "
                    "Currently only binary_little_endian is supported.",
                    format
                );
                return FALSE;
            }
            header->binary_little_endian = TRUE;
            found_format = TRUE;
        }
        else if (strcmp(keyword, "element") == 0) {
            char element[64] = { 0 };
            size_t count = 0;
            if (sscanf(line, "%*s %63s %zu", element, &count) != 2) return FALSE;
            in_vertex = strcmp(element, "vertex") == 0;
            if (in_vertex) {
                header->vertex_count = count;
                found_vertex = TRUE;
            }
        }
        else if (strcmp(keyword, "property") == 0 && in_vertex) {
            char type[64] = { 0 };
            char name[64] = { 0 };
            if (sscanf(line, "%*s %63s %63s", type, name) != 2) {
                logerror("Malformed PLY vertex property");
                return FALSE;
            }
            if (header->property_count >= MAX_PLY_PROPERTIES) {
                logerror("PLY contains too many vertex properties");
                return FALSE;
            }
            PLYType ptype = ParsePLYType(type);
            if (ptype == PLY_TYPE_INVALID) {
                logerror(
                    "Unsupported PLY property type \"%s\" for \"%s\"",
                    type,
                    name
                );
                return FALSE;
            }
            PLYProperty* property = &header->properties[header->property_count++];
            strncpy(property->name, name, sizeof(property->name) - 1);
            property->name[sizeof(property->name) - 1] = '\0';
            property->type = ptype;
        }
        else if (strcmp(keyword, "end_header") == 0) {
            header->data_offset = ftell(file);
            break;
        }
    }
    if (!found_format) {
        logerror("PLY file has no format declaration");
        return FALSE;
    }
    if (!found_vertex) {
        logerror("PLY file has no vertex element");
        return FALSE;
    }
    if (header->property_count == 0) {
        logerror("PLY vertex element contains no properties");
        return FALSE;
    }
    return TRUE;
}

static int FindPLYProperty(const PLYHeader* header, const char* name) {
    for (size_t i = 0; i < header->property_count; i++) {
        if (strcmp(header->properties[i].name, name) == 0)
            return (int)i;
    }
    return -1;
}

static size_t ParseLineArgsOBJ(const char line[MAX_OBJ_LINE_SIZE], char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE]) {
    int numargs = 0;
    int cursor = 0;
    while (IsWhitespace(line[cursor])) cursor++;
    for (int i = cursor; i < MAX_OBJ_LINE_SIZE && line[i] != 0 && line[i] != '#'; i++) {
        if (IsWhitespace(line[i])) {
            memcpy(lineargs[numargs], line + cursor, i - cursor);
            numargs++;
            while (IsWhitespace(line[i])) i++;
            cursor = i;
            if (line[cursor] == '#') break;
        }
    }
    if (line[cursor] != 0 && line[cursor] != '#') {
        memcpy(lineargs[numargs], line + cursor, strnlen(line, MAX_OBJ_LINE_SIZE) - cursor);
        numargs++;
    }
    return numargs;
}

static BOOL ParseTriplet(const char* str, int64_t* a, int64_t* b, int64_t* c, size_t* count) {
    char abuff[MAX_OBJ_ARG_SIZE] = { 0 };
    char bbuff[MAX_OBJ_ARG_SIZE] = { 0 };
    char cbuff[MAX_OBJ_ARG_SIZE] = { 0 };
    *count = 0;
    size_t ptr = 0;
    for (size_t i = 0; i < strlen(str); i++) {
        if (str[i] == '/') {
            if (*count == 0) {
                memcpy(abuff, str + ptr, i - ptr);
                ptr = i + 1;
                *count = 1;
            } else if (*count == 1) {
                memcpy(bbuff, str + ptr, i - ptr);
                ptr = i + 1;
                *count = 2;
            } else {
                return FALSE;
            }
        }
    }
    memcpy(*count == 0 ? abuff : (*count == 1 ? bbuff : cbuff), str + ptr, strlen(str) - ptr);
    *count += 1;
    BOOL success = ez_parse_lint(abuff, a);
    success &= (*a != 0);
    if (success && strlen(bbuff) > 0) {
        success &= ez_parse_lint(bbuff, b) & (*b != 0);
    } else {
        b = 0;
    }
    if (success && strlen(cbuff) > 0) {
        success &= ez_parse_lint(cbuff, c) & (*c != 0);
    } else {
        c = 0;
    }
    return success;
}

static BOOL ParseMTL_illum(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 2) {
        logerror("Cannot parse illumination model (illum) without exactly 1 argument - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    uint32_t a;
    if (!(ez_parse_uint(lineargs[1], &a))) {
        logerror("Invalid illumination model (illum) - expected 1 unsigned integer and got \"%s\" instead", lineargs[1]);
        return FALSE;
    }
    if (a > 10) {
        logerror("Invalid illumination model (illum) - expected a value from 0 to 10, and got \"%s\" instead", lineargs[1]);
        return FALSE;
    }
    if (a != 2 && a != 5 && a != 7) {
        logwarn("Unhandled illumination model \"%d\" (illum) detected - defaulting to lambertian model instead (2)", a);
        a = 2;
    }
    SET_MTL_UINT_FIELD(model, a);
    return TRUE;
}

static BOOL ParseMTL_Ni(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 2) {
        logerror("Cannot parse index of refraction field (Ni) without exactly 1 argument - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float a;
    if (!(ez_parse_float(lineargs[1], &a))) {
        logerror("Invalid index of refraction field (Ni) - expected 1 float and got \"%s\" instead", lineargs[1]);
        return FALSE;
    }
    SET_MTL_FLOAT_FIELD(ior, a);
    return TRUE;
}

static BOOL ParseMTL_Ns(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 2) {
        logerror("Cannot parse specular shininess field (Ns) without exactly 1 argument - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float a;
    if (!(ez_parse_float(lineargs[1], &a))) {
        logerror("Invalid specular shininess field (Ns) - expected 1 float and got \"%s\" instead", lineargs[1]);
        return FALSE;
    }
    SET_MTL_FLOAT_FIELD(shiny, a);
    return TRUE;
}

static BOOL ParseMTL_Ke(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 4) {
        logerror("Cannot parse an emission field (Ke) without exactly 3 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float x, y, z;
    if (!(ez_parse_float(lineargs[1], &x) && ez_parse_float(lineargs[2], &y) && ez_parse_float(lineargs[3], &z))) {
        logerror("Invalid emission (Ke) fields - expected 3 floats and got \"%s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3]);
        return FALSE;
    }
    SET_MTL_VEC3_FIELD(emission, x, y, z);
    return TRUE;
}

static BOOL ParseMTL_Ka(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 4) {
        logerror("Cannot parse an ambience field (Ka) without exactly 3 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float x, y, z;
    if (!(ez_parse_float(lineargs[1], &x) && ez_parse_float(lineargs[2], &y) && ez_parse_float(lineargs[3], &z))) {
        logerror("Invalid ambience (Ka) fields - expected 3 floats and got \"%s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3]);
        return FALSE;
    }
    SET_MTL_VEC3_FIELD(ambient, x, y, z);
    return TRUE;
}

static BOOL ParseMTL_Kd(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 4) {
        logerror("Cannot parse a diffuse field (Kd) without exactly 3 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float x, y, z;
    if (!(ez_parse_float(lineargs[1], &x) && ez_parse_float(lineargs[2], &y) && ez_parse_float(lineargs[3], &z))) {
        logerror("Invalid diffuse (Kd) fields - expected 3 floats and got \"%s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3]);
        return FALSE;
    }
    SET_MTL_VEC3_FIELD(diffuse, x, y, z);
    return TRUE;
}

static BOOL ParseMTL_Ks(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 4) {
        logerror("Cannot parse a specular field (Ks) without exactly 3 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float x, y, z;
    if (!(ez_parse_float(lineargs[1], &x) && ez_parse_float(lineargs[2], &y) && ez_parse_float(lineargs[3], &z))) {
        logerror("Invalid specular (Ks) fields - expected 3 floats and got \"%s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3]);
        return FALSE;
    }
    SET_MTL_VEC3_FIELD(specular, x, y, z);
    return TRUE;
}

static BOOL ParseMTL_Tf(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 4) {
        logerror("Cannot parse an absorbtion field (Tf) without exactly 3 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float x, y, z;
    if (!(ez_parse_float(lineargs[1], &x) && ez_parse_float(lineargs[2], &y) && ez_parse_float(lineargs[3], &z))) {
        logerror("Invalid absorbtion (Tf) fields - expected 3 floats and got \"%s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3]);
        return FALSE;
    }
    SET_MTL_VEC3_FIELD(absorbtion, x, y, z);
    return TRUE;
}

static BOOL ParseMTL_Rd(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 4) {
        logerror("Cannot parse a dispersion field (Rd) without exactly 3 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float x, y, z;
    if (!(ez_parse_float(lineargs[1], &x) && ez_parse_float(lineargs[2], &y) && ez_parse_float(lineargs[3], &z))) {
        logerror("Invalid dispersion (Rd) fields - expected 3 floats and got \"%s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3]);
        return FALSE;
    }
    SET_MTL_VEC3_FIELD(dispersion, x, y, z);
    return TRUE;
}

static BOOL ParseMTL_d(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    // Not useful for prism right now, implement later if needed
    return TRUE;
}

static BOOL ParseMTL_newmtl(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 2) {
        logerror("Invalid format for newmtl arguments, must have only 1 argument - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    char* name = EZ_ALLOC(strlen(lineargs[1]) + 1, sizeof(char));
    strcpy(name, lineargs[1]);
    ARRLIST_DynamicString_add(&(state->material_names), name);
    ARRLIST_SurfaceMaterial_add(&(state->materials), (SurfaceMaterial){ 0 });
    return TRUE;
}

static BOOL ParseMTLTextureMap(const char* line, const char* mtldir, StateOBJ* state) {
    char key[MAX_MTL_ARG_SIZE] = { 0 };
    if (sscanf(line, " %31s", key) != 1) return FALSE;
    int slot = -1;
    BOOL color = TRUE;
    BOOL normal = FALSE;
    if (strcmp(key, "map_Kd") == 0) slot = TEXSLOT_DIFFUSE;
    else if (strcmp(key, "map_Ks") == 0) slot = TEXSLOT_SPECULAR;
    else if (strcmp(key, "map_Ke") == 0) slot = TEXSLOT_EMISSION;
    else if (strcmp(key, "map_Ka") == 0) slot = TEXSLOT_AMBIENT;
    else if (strcmp(key, "map_Ns") == 0) { slot = TEXSLOT_SHINY; color = FALSE; }
    else if (strcmp(key, "map_Ni") == 0) { slot = TEXSLOT_IOR; color = FALSE; }
    else if (strcmp(key, "norm") == 0 || strcmp(key, "map_Bump") == 0 || strcmp(key, "map_bump") == 0 || strcmp(key, "bump") == 0) {
        slot = TEXSLOT_NORMAL;
        color = FALSE;
        normal = TRUE;
    } else if (strncmp(key, "map_", 4) == 0 || strcmp(key, "disp") == 0 || strcmp(key, "decal") == 0 || strcmp(key, "refl") == 0) {
        logwarn("MTL texture map \"%s\" is not supported, skipping it", key);
        return TRUE;
    } else {
        return FALSE;
    }
    if (state->materials.size == 0) {
        logwarn("MTL texture map \"%s\" appears before any newmtl, skipping it", key);
        return TRUE;
    }
    char buf[MAX_MTL_LINE_SIZE] = { 0 };
    strncpy(buf, line, MAX_MTL_LINE_SIZE - 1);
    char* hash = strchr(buf, '#');
    if (hash) *hash = '\0';
    size_t len = strlen(buf);
    while (len > 0 && IsWhitespace(buf[len - 1])) buf[--len] = '\0';
    char* name = buf + len;
    while (name > buf && !IsWhitespace(name[-1])) name--;
    if (!name[0] || strcmp(name, key) == 0) {
        logwarn("MTL texture map \"%s\" has no file name", key);
        return TRUE;
    }
    for (char* c = name; *c; c++) if (*c == '\\') *c = '/';
    char path[MAX_TEXTURE_PATH_SIZE] = { 0 };
    snprintf(path, sizeof(path), "%s%s", name[0] == '/' ? "" : mtldir, name);
    size_t before = NumTextures();
    size_t tex = LoadTextureFile(path, TRUE);
    if (tex == (size_t)-1) {
        logwarn("Unable to load texture \"%s\" referenced by %s, the material keeps its plain values", path, key);
        return TRUE;
    }
    if (NumTextures() > before) {
        SceneTexture* st = TextureReference(tex);
        if (!color) st->settings.srgb = FALSE;
        if (normal) st->settings.normalmap = TRUE;
    }
    SurfaceMaterial* m = &(state->materials.data[state->materials.size - 1]);
    m->textures[slot] = TextureHandleOf(tex);
    float* base = NULL;
    switch (slot) {
        case TEXSLOT_DIFFUSE: base = m->diffuse; break;
        case TEXSLOT_SPECULAR: base = m->specular; break;
        case TEXSLOT_EMISSION: base = m->emission; break;
        case TEXSLOT_AMBIENT: base = m->ambient; break;
        default: break;
    }
    if (base && base[0] == 0.0f && base[1] == 0.0f && base[2] == 0.0f) { base[0] = 1.0f; base[1] = 1.0f; base[2] = 1.0f; }
    if (slot == TEXSLOT_SHINY && m->shiny == 0.0f) m->shiny = 1.0f;
    if (slot == TEXSLOT_IOR && m->ior == 0.0f) m->ior = 1.0f;
    UpdateTextures();
    return TRUE;
}

static ParseFuncMTL GetParserFromArgMTL(const char* header) {
    #define GETPARSER(h) if (strcmp(header, #h) == 0) return ParseMTL_##h;
    GETPARSER(newmtl);
    GETPARSER(illum);
    GETPARSER(Ke);
    GETPARSER(Ka);
    GETPARSER(Ks);
    GETPARSER(Kd);
    GETPARSER(Ns);
    GETPARSER(Ni);
    GETPARSER(Tf);
    GETPARSER(Rd);
    GETPARSER(d);
    #undef GETPARSER
    return NULL;
}

static BOOL ParseOBJ_mtllib(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 2) {
        logerror("Invalid format for mtllib arguments, must have only 1 argument - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    char mtlloc[MAX_MTLLIB_PATH_SIZE] = { 0 };
    char mtlpath[MAX_MTLLIB_PATH_SIZE] = { 0 };
    strcpy(mtlloc, state->filepath);
    char* fnstart = (char*)ez_strip_filename(mtlloc);
    if (fnstart) fnstart[0] = 0;
    else mtlloc[0] = 0;
    sprintf(mtlpath, "%s%s", mtlloc, lineargs[1]);
    ez_File* file = ez_load_file(mtlpath);
    if (!file) {
        logerror("Unable to load invalid filepath to mtllib \"%s\"", mtlpath);
        return FALSE;
    }
    if (file->type != DOTMTL) {
        logerror("\"%s\" is not a .mtl file. Unable to open it with the MTL loader", mtlpath);
        ez_free_file(file);
        return FALSE;
    }
    ez_FileParser parser = ez_parser(file);
    char line[MAX_MTL_LINE_SIZE] = { 0 };
    BOOL failure = FALSE;
    char mtldir[MAX_MTLLIB_PATH_SIZE] = { 0 };
    strcpy(mtldir, mtlpath);
    char* mtlname = (char*)ez_strip_filename(mtldir);
    if (mtlname) mtlname[0] = 0;
    else mtldir[0] = 0;
    while (ez_next_line(&parser, line, MAX_MTL_LINE_SIZE)) {
        if (ParseMTLTextureMap(line, mtldir, state)) continue;
        char lineargs[MAX_MTL_NUM_ARGS][MAX_MTL_ARG_SIZE] = { 0 };
        size_t numargs = ParseLineArgsOBJ(line, lineargs);
        if (numargs > 0 && lineargs[0][0] != '#') {
            ParseFuncMTL p = GetParserFromArgMTL(lineargs[0]);
            if (p) { if ((failure = !p(lineargs, numargs, state))) { logerror("%s:%d - Unable to parse this MTL field due to an error", mtlpath, (int)parser.line); break; }
            } else logwarn("%s:%d - Unknown MTL property detected: \"%s\", skipping parsing this field...", mtlpath, (int)parser.line, lineargs[0]);
        }
    }
    ez_free_file(file);
    return !failure;
}

static BOOL ParseOBJ_usemtl(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 2) {
        logerror("Invalid format for usemtl arguments, must have only 1 argument - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    size_t ind = 0;
    for (size_t i = 0; i < state->material_names.size; i++) {
        if (strcmp(state->material_names.data[i], lineargs[1]) == 0) {
            ind = i;
            goto found;
        }
    }
    logerror("No material of name \"%s\" found", lineargs[1]);
    return FALSE;
    found:
    ARRLIST_UseMaterialMarker_add(&(state->markers), (UseMaterialMarker){ state->faces.size, ind });
    return TRUE;
}

static BOOL ParseOBJ_v(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 4) {
        logerror("Cannot parse a vertex field without exactly 3 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    vec3 v;
    if (!(ez_parse_float(lineargs[1], &(v[0])) && ez_parse_float(lineargs[2], &(v[1])) && ez_parse_float(lineargs[3], &(v[2])))) {
        logerror("Invalid vertex fields - expected 3 floats and got \"%s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3]);
        return FALSE;
    }
    ARRLIST_vec3_add(&(state->vertices), v);
    return TRUE;
}

static BOOL ParseOBJ_vn(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 4) {
        logerror("Cannot parse a vertex normal field without exactly 3 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    vec3 v;
    if (!(ez_parse_float(lineargs[1], &(v[0])) && ez_parse_float(lineargs[2], &(v[1])) && ez_parse_float(lineargs[3], &(v[2])))) {
        logerror("Invalid vertex normal fields - expected 3 floats and got \"%s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3]);
        return FALSE;
    }
    ARRLIST_vec3_add(&(state->normals), v);
    return TRUE;
}

static BOOL ParseOBJ_vt(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 3 && numargs != 4) {
        logerror("Cannot parse a vertex texture field without 2-3 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float u, v; // ignoring w
    if (!(ez_parse_float(lineargs[1], &u) && ez_parse_float(lineargs[2], &v))) {
        logerror("Invalid vertex texture fields - expected 2 floats and got \"%s %s\" instead", lineargs[1], lineargs[2]);
        return FALSE;
    }
    ARRLIST_UV_add(&(state->uvs), (UV){u, v});
    return TRUE;
}

static BOOL ParseOBJ_f(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 4 && numargs != 5) {
        logerror("Cannot parse a face field without exactly 3 or 4 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    int64_t values[4][3] = { 0 };
    int count = -1;
    for (int i = 0; i < (numargs == 4 ? 3 : 4); i++) {
        size_t c;
        if (!ParseTriplet(lineargs[i + 1], &values[i][0], &values[i][1], &values[i][2], &c)) {
            logerror("Unable to parse face field \"%s\" into valid indices", lineargs[i + 1]);
            return FALSE;
        }
        if (count < 0) {
            count = (int)c;
        } else if (count != (int)c) {
            logerror("Detected a mismatched count in face field indices - expected %d but got %d instead", count, (int)c);
            return FALSE;
        }
    }
    for (int i = 0; i < 2; i++) {
        int zeros = 0;
        for (int j = 0; j < (numargs == 4 ? 3 : 4); j++) {
            if (values[j][i] == 0) zeros++;
        }
        if (zeros != 0 && zeros != (numargs == 4 ? 3 : 4)) {
            logerror("Detected a mismatched format count in face field indices - vertex normals/textures should either be all defined or not at all");
            return FALSE;
        }
    }
    for (int i = 0; i < (numargs == 4 ? 3 : 4); i++) {
        if (values[i][0] == 0) {
            logerror("Invalid face field detected - there is no vertex 0");
            return FALSE;
        }
        if (values[i][0] < 0) {
            values[i][0] = state->vertices.size + values[i][0];
        } else {
            values[i][0]--;
        }
        if (values[i][1] < 0) {
            values[i][1] = state->uvs.size + values[i][1];
        } else if (values[i][1] > 0) {
            values[i][1]--;
        } else {
            values[i][1] = (size_t)-1;
        }
        if (values[i][2] < 0) {
            values[i][2] = state->normals.size + values[i][2];
        } else if (values[i][2] > 0) {
            values[i][2]--;
        } else {
            values[i][2] = (size_t)-1;
        }
    }
    ARRLIST_Face_add(&(state->faces), (Face){
        values[0][0], values[1][0], values[2][0],
        values[0][1], values[1][1], values[2][1],
        values[0][2], values[1][2], values[2][2],
        values[0][1] != -1, values[0][2] != -1
    });
    if (numargs == 5) ARRLIST_Face_add(&(state->faces), (Face){
        values[0][0], values[2][0], values[3][0],
        values[0][1], values[2][1], values[3][1],
        values[0][2], values[2][2], values[3][2],
        values[0][1] != -1, values[0][2] != -1
    });
    return TRUE;
}

static BOOL ParseOBJ_g(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    // Not useful for prism right now, implement later if needed
    return TRUE;
}

static BOOL ParseOBJ_o(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    // Not useful for prism right now, implement later if needed
    return TRUE;
}

static BOOL ParseOBJ_s(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    // Not useful for prism right now, implement later if needed
    return TRUE;
}

static ParseFuncOBJ GetParserFromArgOBJ(const char* header) {
    #define GETPARSER(h) if (strcmp(header, #h) == 0) return ParseOBJ_##h;
    GETPARSER(mtllib);
    GETPARSER(usemtl);
    GETPARSER(v);
    GETPARSER(vn);
    GETPARSER(vt);
    GETPARSER(f);
    GETPARSER(g);
    GETPARSER(o);
    GETPARSER(s);
    #undef GETPARSER
    return NULL;
}

static BOOL ConstructOBJ(const StateOBJ state) {
    BOOL failure = FALSE;
    for (size_t i = 0; i < state.faces.size; i++) {
        if (state.faces.data[i].a >= state.vertices.size) {
            logerror("Face %d references an invalid vertex %d - there are only %d vertices available", (int)i, (int)state.faces.data[i].a, (int)state.vertices.size);
            failure = TRUE;
        }
        if (state.faces.data[i].b >= state.vertices.size) {
            logerror("Face %d references an invalid vertex %d - there are only %d vertices available", (int)i, (int)state.faces.data[i].b, (int)state.vertices.size);
            failure = TRUE;
        }
        if (state.faces.data[i].c >= state.vertices.size) {
            logerror("Face %d references an invalid vertex normal %d - there are only %d vertices available", (int)i, (int)state.faces.data[i].c, (int)state.vertices.size);
            failure = TRUE;
        }
        if (state.faces.data[i].normals && state.faces.data[i].an >= state.normals.size) {
            logerror("Face %d references an invalid vertex normal %d - there are only %d normals available", (int)i, (int)state.faces.data[i].an, (int)state.normals.size);
            failure = TRUE;
        }
        if (state.faces.data[i].normals && state.faces.data[i].bn >= state.normals.size) {
            logerror("Face %d references an invalid vertex normal %d - there are only %d normals available", (int)i, (int)state.faces.data[i].bn, (int)state.normals.size);
            failure = TRUE;
        }
        if (state.faces.data[i].normals && state.faces.data[i].cn >= state.normals.size) {
            logerror("Face %d references an invalid vertex normal %d - there are only %d normals available", (int)i, (int)state.faces.data[i].cn, (int)state.normals.size);
            failure = TRUE;
        }
        if (state.faces.data[i].textures &&
            (state.faces.data[i].at >= state.uvs.size || state.faces.data[i].bt >= state.uvs.size || state.faces.data[i].ct >= state.uvs.size)) {
            logerror("Face %d references an invalid texture coordinate - there are only %d available", (int)i, (int)state.uvs.size);
            failure = TRUE;
        }
    }
    if (failure) return FALSE;
    ARRLIST_MaterialID ids = { 0 };
    size_t vertices_start = NumVertices();
    size_t normals_start = NumNormals();
    size_t uvs_start = NumUVs();
    for (size_t i = 0; i < state.materials.size; i++)
        ARRLIST_MaterialID_add(&ids, SubmitNamedMaterial(state.materials.data[i], state.material_names.data[i]));
    for (size_t i = 0; i < state.vertices.size; i++)
        SubmitVertex(state.vertices.data[i]);
    for (size_t i = 0; i < state.normals.size; i++)
        SubmitNormal(state.normals.data[i]);
    for (size_t i = 0; i < state.uvs.size; i++)
        SubmitUV((vec2){ state.uvs.data[i].u, state.uvs.data[i].v });
    size_t current_marker = 0;
    MaterialID current_material = 0;
    for (size_t i = 0; i < state.faces.size; i++) {
        while (current_marker < state.markers.size && state.markers.data[current_marker].face_index <= i) {
            current_material = ids.data[state.markers.data[current_marker].material_ind];
            current_marker++;
        }
        Triangle triangle = {
            vertices_start + state.faces.data[i].a,
            vertices_start + state.faces.data[i].b,
            vertices_start + state.faces.data[i].c,
            state.normals.size > 0 && state.faces.data[i].normals ? normals_start + state.faces.data[i].an : (uint32_t)-1,
            state.normals.size > 0 && state.faces.data[i].normals ? normals_start + state.faces.data[i].bn : (uint32_t)-1,
            state.normals.size > 0 && state.faces.data[i].normals ? normals_start + state.faces.data[i].cn : (uint32_t)-1,
            current_material,
            NO_UVS
        };
        if (state.faces.data[i].textures) {
            triangle.at = (uint32_t)(uvs_start + state.faces.data[i].at);
            triangle.bt = (uint32_t)(uvs_start + state.faces.data[i].bt);
            triangle.ct = (uint32_t)(uvs_start + state.faces.data[i].ct);
        }
        SubmitTriangle(triangle);
    }
    ARRLIST_MaterialID_clear(&ids);
    return TRUE;
}

static void PrismWrite(PrismStream* s, const void* data, size_t size) {
    if (!s->ok || size == 0) return;
    if (fwrite(data, 1, size, s->file) != size) s->ok = FALSE;
}

static void PrismRead(PrismStream* s, void* data, size_t size) {
    if (!s->ok || size == 0) return;
    if (size > s->remaining || fread(data, 1, size, s->file) != size) { s->ok = FALSE; return; }
    s->remaining -= size;
}

static BOOL PrismFits(PrismStream* s, size_t count, size_t elemsize) {
    if (!s->ok) return FALSE;
    if (elemsize != 0 && count > s->remaining / elemsize) { s->ok = FALSE; return FALSE; }
    return TRUE;
}

static void PrismWriteSize(PrismStream* s, size_t v) {
    uint64_t u = (uint64_t)v;
    PrismWrite(s, &u, sizeof(u));
}

static size_t PrismReadSize(PrismStream* s) {
    uint64_t u = 0;
    PrismRead(s, &u, sizeof(u));
    return s->ok ? (size_t)u : 0;
}

static void* PrismReadElements(PrismStream* s, size_t count, size_t elemsize) {
    if (count == 0 || !PrismFits(s, count, elemsize)) return NULL;
    void* ptr = EZ_ALLOC(count, elemsize);
    PrismRead(s, ptr, count * elemsize);
    return ptr;
}

static void PrismWriteString(PrismStream* s, const char* str) {
    size_t len = str ? strlen(str) : 0;
    PrismWriteSize(s, len);
    PrismWrite(s, str, len);
}

static char* PrismReadString(PrismStream* s, size_t maxlen, size_t minsize) {
    size_t len = PrismReadSize(s);
    if (!s->ok) return NULL;
    if (len > maxlen) { s->ok = FALSE; return NULL; }
    size_t alloc = (len > minsize ? len : minsize) + 1;
    char* str = EZ_ALLOC(alloc, sizeof(char));
    PrismRead(s, str, len);
    return str;
}

static void PrismWriteNames(PrismStream* s, ARRLIST_DynamicString* names) {
    PrismWriteSize(s, names->size);
    for (size_t i = 0; i < names->size; i++) PrismWriteString(s, names->data[i]);
}

static void PrismReadNames(PrismStream* s, ARRLIST_DynamicString* names, size_t maxlen) {
    *names = (ARRLIST_DynamicString){ 0 };
    size_t n = PrismReadSize(s);
    if (n == 0 || !PrismFits(s, n, sizeof(uint64_t))) return;
    names->data = EZ_ALLOC(n, sizeof(DynamicString));
    names->size = n;
    names->maxsize = n;
    for (size_t i = 0; i < n && s->ok; i++) names->data[i] = PrismReadString(s, maxlen, maxlen);
}

static void PrismFreeNames(ARRLIST_DynamicString* names) {
    if (names->data) for (size_t i = 0; i < names->size; i++) EZ_FREE(names->data[i]);
    ARRLIST_DynamicString_clear(names);
}

static void PrismWriteAnimations(PrismStream* s, ARRLIST_MeshAnimation* anims) {
    PrismWriteSize(s, anims->size);
    for (size_t i = 0; i < anims->size; i++) {
        MeshAnimation local;
        memcpy(&local, &(anims->data[i]), sizeof(MeshAnimation));
        MeshAnimation* ma = &local;
        PrismWrite(s, ma, sizeof(MeshAnimation));
        PrismWriteSize(s, ma->animations.size);
        for (size_t j = 0; j < ma->animations.size; j++) {
            Animation* a = &(ma->animations.data[j]);
            PrismWrite(s, a, sizeof(Animation));
            PrismWriteSize(s, a->channels.size);
            for (size_t k = 0; k < a->channels.size; k++) {
                BoneChannel* bc = &(a->channels.data[k]);
                PrismWrite(s, bc, sizeof(BoneChannel));
                PRISM_WRITE_LIST(s, bc->positions);
                PRISM_WRITE_LIST(s, bc->rotations);
                PRISM_WRITE_LIST(s, bc->scales);
            }
            PrismWriteString(s, j < ma->names.size ? ma->names.data[j] : a->name);
        }
    }
}

static void PrismFreeAnimations(ARRLIST_MeshAnimation* anims) {
    if (anims->data) {
        for (size_t i = 0; i < anims->size; i++) {
            MeshAnimation local;
            memcpy(&local, &(anims->data[i]), sizeof(MeshAnimation));
            MeshAnimation* ma = &local;
            if (ma->animations.data) {
                for (size_t j = 0; j < ma->animations.size; j++) {
                    Animation* a = &(ma->animations.data[j]);
                    if (a->channels.data) {
                        for (size_t k = 0; k < a->channels.size; k++) {
                            ARRLIST_Vec3Key_clear(&(a->channels.data[k].positions));
                            ARRLIST_QuatKey_clear(&(a->channels.data[k].rotations));
                            ARRLIST_Vec3Key_clear(&(a->channels.data[k].scales));
                        }
                    }
                    ARRLIST_BoneChannel_clear(&(a->channels));
                }
            }
            ARRLIST_Animation_clear(&(ma->animations));
            PrismFreeNames(&(ma->names));
        }
    }
    ARRLIST_MeshAnimation_clear(anims);
}

static void PrismReadAnimations(PrismStream* s, ARRLIST_MeshAnimation* anims) {
    *anims = (ARRLIST_MeshAnimation){ 0 };
    size_t n = PrismReadSize(s);
    if (n == 0 || !PrismFits(s, n, sizeof(MeshAnimation))) return;
    anims->data = EZ_ALLOC(n, sizeof(MeshAnimation));
    anims->size = n;
    anims->maxsize = n;
    for (size_t i = 0; i < n && s->ok; i++) {
        MeshAnimation local;
        MeshAnimation* ma = &local;
        PrismRead(s, ma, sizeof(MeshAnimation));
        ma->animations = (ARRLIST_Animation){ 0 };
        ma->names = (ARRLIST_DynamicString){ 0 };
        memcpy(&(anims->data[i]), ma, sizeof(MeshAnimation));
        if (!s->ok) break;
        size_t na = PrismReadSize(s);
        if (na == 0 || !PrismFits(s, na, sizeof(Animation))) continue;
        ma->animations.data = EZ_ALLOC(na, sizeof(Animation));
        ma->animations.size = na;
        ma->animations.maxsize = na;
        ma->names.data = EZ_ALLOC(na, sizeof(DynamicString));
        ma->names.size = na;
        ma->names.maxsize = na;
        memcpy(&(anims->data[i]), ma, sizeof(MeshAnimation));
        for (size_t j = 0; j < na && s->ok; j++) {
            Animation* a = &(ma->animations.data[j]);
            PrismRead(s, a, sizeof(Animation));
            a->channels = (ARRLIST_BoneChannel){ 0 };
            if (!s->ok) break;
            size_t nc = PrismReadSize(s);
            if (nc > 0 && PrismFits(s, nc, sizeof(BoneChannel))) {
                a->channels.data = EZ_ALLOC(nc, sizeof(BoneChannel));
                a->channels.size = nc;
                a->channels.maxsize = nc;
                for (size_t k = 0; k < nc && s->ok; k++) {
                    BoneChannel* bc = &(a->channels.data[k]);
                    PrismRead(s, bc, sizeof(BoneChannel));
                    bc->positions = (ARRLIST_Vec3Key){ 0 };
                    bc->rotations = (ARRLIST_QuatKey){ 0 };
                    bc->scales = (ARRLIST_Vec3Key){ 0 };
                    PRISM_READ_LIST(s, bc->positions);
                    PRISM_READ_LIST(s, bc->rotations);
                    PRISM_READ_LIST(s, bc->scales);
                }
            }
            ma->names.data[j] = PrismReadString(s, sizeof(a->name) - 1, 0);
        }
    }
}

static void PrismFreeGeometry(Geometry* g) {
    ARRLIST_vec4_clear(&(g->vertices));
    ARRLIST_vec4_clear(&(g->normals));
    ARRLIST_vec2_clear(&(g->uvs));
    ARRLIST_SceneLight_clear(&(g->lights));
    PrismFreeNames(&(g->lightnames));
    ARRLIST_Triangle_clear(&(g->triangles));
    ARRLIST_GaussianSplat_clear(&(g->splats));
    ARRLIST_TriangleID_clear(&(g->emissives));
    ARRLIST_EmissionAlias_clear(&(g->aliases));
    ARRLIST_SurfaceMaterial_clear(&(g->materials));
    PrismFreeNames(&(g->materialnames));
    ARRLIST_Skeleton_clear(&(g->skeletons));
    PrismFreeAnimations(&(g->animations));
    ARRLIST_VertexSkin_clear(&(g->skins));
    ARRLIST_mat4_clear(&(g->poses));
    HASHMAP_Locks_clear(&(g->locks));
    HASHMAP_EdgeGlue_clear(&(g->glue));
    ARRLIST_Edge_clear(&(g->edges));
    ARRLIST_MeshDescriptor_clear(&(g->meshes));
    PrismFreeNames(&(g->meshnames));
    ARRLIST_SceneCamera_clear(&(g->cameras));
    PrismFreeNames(&(g->cameranames));
}

static void PrismWriteGeometry(PrismStream* s, Geometry* g) {
    PrismWrite(s, g, sizeof(Geometry));
    PRISM_WRITE_LIST(s, g->vertices);
    PRISM_WRITE_LIST(s, g->normals);
    PRISM_WRITE_LIST(s, g->uvs);
    PRISM_WRITE_LIST(s, g->lights);
    PrismWriteNames(s, &(g->lightnames));
    PRISM_WRITE_LIST(s, g->triangles);
    PRISM_WRITE_LIST(s, g->splats);
    PRISM_WRITE_LIST(s, g->emissives);
    PRISM_WRITE_LIST(s, g->aliases);
    PRISM_WRITE_LIST(s, g->materials);
    PrismWriteNames(s, &(g->materialnames));
    PRISM_WRITE_LIST(s, g->skeletons);
    PrismWriteAnimations(s, &(g->animations));
    PRISM_WRITE_LIST(s, g->skins);
    PRISM_WRITE_LIST(s, g->poses);
    PRISM_WRITE_MAP(s, g->locks);
    PRISM_WRITE_MAP(s, g->glue);
    PRISM_WRITE_LIST(s, g->edges);
    PRISM_WRITE_LIST(s, g->meshes);
    PrismWriteNames(s, &(g->meshnames));
    PRISM_WRITE_LIST(s, g->cameras);
    PrismWriteNames(s, &(g->cameranames));
}

static void PrismReadGeometry(PrismStream* s, Geometry* g) {
    PrismRead(s, g, sizeof(Geometry));
    g->manifold = (ManifoldMesh){ 0 };
    g->arap = (ARAPData){ 0 };
    g->fluid = (FluidSimulation){ 0 };
    g->changes = (ChangeSet){ 0 };
    g->vertices = (ARRLIST_vec4){ 0 };
    g->normals = (ARRLIST_vec4){ 0 };
    g->uvs = (ARRLIST_vec2){ 0 };
    g->lights = (ARRLIST_SceneLight){ 0 };
    g->lightnames = (ARRLIST_DynamicString){ 0 };
    g->triangles = (ARRLIST_Triangle){ 0 };
    g->splats = (ARRLIST_GaussianSplat){ 0 };
    g->emissives = (ARRLIST_TriangleID){ 0 };
    g->aliases = (ARRLIST_EmissionAlias){ 0 };
    g->materials = (ARRLIST_SurfaceMaterial){ 0 };
    g->materialnames = (ARRLIST_DynamicString){ 0 };
    g->skeletons = (ARRLIST_Skeleton){ 0 };
    g->animations = (ARRLIST_MeshAnimation){ 0 };
    g->skins = (ARRLIST_VertexSkin){ 0 };
    g->poses = (ARRLIST_mat4){ 0 };
    HASHMAP_Locks locks = g->locks;
    HASHMAP_EdgeGlue glue = g->glue;
    g->locks = (HASHMAP_Locks){ 0 };
    g->glue = (HASHMAP_EdgeGlue){ 0 };
    g->edges = (ARRLIST_Edge){ 0 };
    g->meshes = (ARRLIST_MeshDescriptor){ 0 };
    g->meshnames = (ARRLIST_DynamicString){ 0 };
    g->cameras = (ARRLIST_SceneCamera){ 0 };
    g->cameranames = (ARRLIST_DynamicString){ 0 };
    g->gsss = (ARRLIST_uint32_t){ 0 };
    g->textures = (ARRLIST_uint32_t){ 0 };
    if (!s->ok) return;
    PRISM_READ_LIST(s, g->vertices);
    PRISM_READ_LIST(s, g->normals);
    PRISM_READ_LIST(s, g->uvs);
    PRISM_READ_LIST(s, g->lights);
    PrismReadNames(s, &(g->lightnames), MAX_LIGHT_NAME_SIZE);
    PRISM_READ_LIST(s, g->triangles);
    PRISM_READ_LIST(s, g->splats);
    PRISM_READ_LIST(s, g->emissives);
    PRISM_READ_LIST(s, g->aliases);
    PRISM_READ_LIST(s, g->materials);
    PrismReadNames(s, &(g->materialnames), MAX_MATERIAL_NAME_SIZE);
    PRISM_READ_LIST(s, g->skeletons);
    PrismReadAnimations(s, &(g->animations));
    PRISM_READ_LIST(s, g->skins);
    PRISM_READ_LIST(s, g->poses);
    g->locks.size = locks.size;
    g->locks.tombstones = locks.tombstones;
    PRISM_READ_MAP(s, g->locks);
    g->glue.size = glue.size;
    g->glue.tombstones = glue.tombstones;
    PRISM_READ_MAP(s, g->glue);
    PRISM_READ_LIST(s, g->edges);
    PRISM_READ_LIST(s, g->meshes);
    PrismReadNames(s, &(g->meshnames), MAX_MESH_NAME_SIZE);
    PRISM_READ_LIST(s, g->cameras);
    PrismReadNames(s, &(g->cameranames), MAX_CAMERA_NAME_SIZE);
    if (!s->ok) return;
    if (g->lightnames.size != g->lights.size ||
        g->materialnames.size != g->materials.size ||
        g->meshnames.size != g->meshes.size ||
        g->cameranames.size != g->cameras.size) s->ok = FALSE;
    for (size_t i = 0; s->ok && i < g->triangles.size; i++) {
        Triangle t = g->triangles.data[i];
        if (t.a >= g->vertices.size || t.b >= g->vertices.size || t.c >= g->vertices.size ||
            t.material >= g->materials.size ||
            (t.at != NO_UV && t.at >= g->uvs.size) || (t.bt != NO_UV && t.bt >= g->uvs.size) ||
            (t.ct != NO_UV && t.ct >= g->uvs.size)) s->ok = FALSE;
    }
    for (size_t i = 0; s->ok && i < g->meshes.size; i++) {
        MeshDescriptor* md = &(g->meshes.data[i]);
        if (!(md->send < md->sstart || md->send == (uint32_t)-1) && md->send >= g->splats.size) s->ok = FALSE;
    }
    if (s->ok && g->cameras.size > 0 && g->primarycamera >= g->cameras.size) g->primarycamera = 0;
}

static size_t PrismTextureLevelBytes(const TextureSettings* settings, const TextureLevel* level) {
    return (size_t)level->width * level->height * 4 * (settings->hdr ? sizeof(float) : sizeof(uint8_t));
}

static void PrismWriteTexture(PrismStream* s, size_t index) {
    SceneTexture* t = TextureReference(index);
    PrismWriteString(s, TextureName(index));
    PrismWriteString(s, t->source);
    PrismWrite(s, &(t->settings), sizeof(TextureSettings));
    for (uint32_t l = 0; l < t->settings.levels; l++) {
        uint32_t dims[2] = { t->mips[l].width, t->mips[l].height };
        PrismWrite(s, dims, sizeof(dims));
        PrismWrite(s, t->mips[l].data, PrismTextureLevelBytes(&(t->settings), &(t->mips[l])));
    }
}

static BOOL PrismReadTexture(PrismStream* s, SceneTexture* t, char** name) {
    *t = (SceneTexture){ 0 };
    *name = PrismReadString(s, MAX_TEXTURE_NAME_SIZE, 0);
    char* source = PrismReadString(s, MAX_TEXTURE_PATH_SIZE - 1, 0);
    if (source) {
        strncpy(t->source, source, MAX_TEXTURE_PATH_SIZE - 1);
        EZ_FREE(source);
    }
    PrismRead(s, &(t->settings), sizeof(TextureSettings));
    if (!s->ok) return FALSE;
    TextureSettings* st = &(t->settings);
    if (st->levels < 1 || st->levels > MAX_TEXTURE_LEVELS || st->width < 1 || st->height < 1 ||
        st->width > MAX_TEXTURE_DIMENSION || st->height > MAX_TEXTURE_DIMENSION) {
        s->ok = FALSE;
        return FALSE;
    }
    uint32_t w = st->width, h = st->height;
    for (uint32_t l = 0; l < st->levels && s->ok; l++) {
        uint32_t dims[2] = { 0 };
        PrismRead(s, dims, sizeof(dims));
        if (!s->ok || dims[0] != w || dims[1] != h) { s->ok = FALSE; break; }
        t->mips[l].width = w;
        t->mips[l].height = h;
        t->mips[l].data = PrismReadElements(s, (size_t)w * h * 4, st->hdr ? sizeof(float) : sizeof(uint8_t));
        if (!t->mips[l].data) s->ok = FALSE;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    return s->ok;
}

static void PrismWriteTextureList(PrismStream* s, const TextureHandle* handles, size_t count) {
    PrismWriteSize(s, count);
    for (size_t i = 0; i < count; i++) PrismWriteTexture(s, handles[i] - 1);
}

static void PrismFreeTextureSet(PrismTextureSet* set) {
    for (size_t i = 0; i < set->count; i++) {
        if (set->textures) FreeSceneTexture(&(set->textures[i]));
        if (set->names && set->names[i]) EZ_FREE(set->names[i]);
    }
    if (set->textures) EZ_FREE(set->textures);
    if (set->names) EZ_FREE(set->names);
    if (set->map) EZ_FREE(set->map);
    *set = (PrismTextureSet){ 0 };
}

static void PrismReadTextureSet(PrismStream* s, PrismTextureSet* set) {
    *set = (PrismTextureSet){ 0 };
    size_t n = PrismReadSize(s);
    if (n == 0 || !PrismFits(s, n, sizeof(TextureSettings))) return;
    set->textures = EZ_ALLOC(n, sizeof(SceneTexture));
    set->names = EZ_ALLOC(n, sizeof(char*));
    set->map = EZ_ALLOC(n, sizeof(TextureHandle));
    set->count = n;
    for (size_t i = 0; i < n && s->ok; i++) PrismReadTexture(s, &(set->textures[i]), &(set->names[i]));
}

static void PrismSubmitTextureSet(PrismTextureSet* set) {
    for (size_t i = 0; i < set->count; i++) {
        size_t index = SubmitNamedTexture(set->textures[i], set->names[i] && set->names[i][0] ? set->names[i] : "Texture");
        set->map[i] = TextureHandleOf(index);
        set->textures[i] = (SceneTexture){ 0 };
    }
}

static TextureHandle PrismRemapTexture(const PrismTextureSet* set, TextureHandle local) {
    if (local == 0 || local > set->count || !set->map) return 0;
    return set->map[local - 1];
}

static TextureHandle PrismCollectTexture(ARRLIST_uint32_t* handles, TextureHandle global) {
    if (global == 0 || global > NumTextures()) return 0;
    for (size_t i = 0; i < handles->size; i++) if (handles->data[i] == global) return (TextureHandle)(i + 1);
    ARRLIST_uint32_t_add(handles, global);
    return (TextureHandle)handles->size;
}

static void PrismCollectMaterialTextures(ARRLIST_uint32_t* handles, SurfaceMaterial* m) {
    for (size_t k = 0; k < MATERIAL_TEXTURE_SLOTS; k++) m->textures[k] = PrismCollectTexture(handles, m->textures[k]);
}

static void PrismRemapMaterialTextures(const PrismTextureSet* set, SurfaceMaterial* m) {
    for (size_t k = 0; k < MATERIAL_TEXTURE_SLOTS; k++) m->textures[k] = PrismRemapTexture(set, m->textures[k]);
}

static const char* PbjTypeName(PBJType type) {
    switch (type) {
        case PBJ_CAMERA: return "camera";
        case PBJ_MATERIAL: return "material";
        case PBJ_LIGHT: return "light";
        case PBJ_MESH: return "object";
        case PBJ_FORCE: return "force";
        case PBJ_SOURCE: return "source";
        case PBJ_TEXTURE: return "texture";
        default: return "unknown item";
    }
}

static void PbjFreeMesh(PBJMesh* m) {
    ARRLIST_Triangle_clear(&m->triangles);
    ARRLIST_vec4_clear(&m->vertices);
    ARRLIST_vec3_clear(&m->normals);
    ARRLIST_vec2_clear(&m->uvs);
    ARRLIST_SurfaceMaterial_clear(&m->materials);
    PrismFreeNames(&m->materialnames);
    ARRLIST_GaussianSplat_clear(&m->splats);
    PrismFreeAnimations(&m->anims);
    ARRLIST_VertexSkin_clear(&m->skins);
    ARRLIST_mat4_clear(&m->pose);
    if (m->sss) EZ_FREE(m->sss);
    m->sss = NULL;
    m->sss_words = 0;
}

static void PbjWriteMesh(PrismStream* s, size_t id) {
    Geometry* geo = RendererGeometry();
    MeshDescriptor* original = MeshReference(id);
    MeshDescriptor copy;
    memcpy((void*)&copy, (const void*)original, sizeof(MeshDescriptor)); // heap descriptors are only 8 byte aligned
    BOOL has_tris = !(original->tend < original->tstart || original->tend == (TriangleID)-1);
    BOOL has_splats = !(original->send < original->sstart || original->send == (uint32_t)-1);
    BOOL animated = original->pose != (uint32_t)-1;
    if (animated && (original->pose + MAX_BONES > geo->poses.size ||
                     original->pose / MAX_BONES >= NumAnimations() ||
                     (size_t)original->skinstart + (original->end - original->start) >= geo->skins.size)) {
        logwarn("Animation data of object %zu is inconsistent, saving it without animation", id);
        animated = FALSE;
    }
    ARRLIST_Triangle triangles = { 0 };
    ARRLIST_vec4 vertices = { 0 };
    ARRLIST_VertexSkin skins = { 0 };
    HASHMAP_Index vertexmap = { 0 };
    ARRLIST_vec3 normals = { 0 };
    HASHMAP_Index normalmap = { 0 };
    ARRLIST_vec2 uvs = { 0 };
    HASHMAP_Index uvmap = { 0 };
    ARRLIST_SurfaceMaterial materials = { 0 };
    ARRLIST_DynamicString materialnames = { 0 };
    HASHMAP_Index materialmap = { 0 };
    ARRLIST_GaussianSplat splats = { 0 };
    if (has_tris) {
        for (TriangleID i = original->tstart; i <= original->tend; i++) {
            Triangle t = *(TriangleReference(i));
            uint32_t* verts[] = { &t.a, &t.b, &t.c };
            uint32_t* norms[] = { &t.an, &t.bn, &t.cn };
            uint32_t* texs[] = { &t.at, &t.bt, &t.ct };
            for (size_t j = 0; j < 3; j++) {
                if (!HASHMAP_Index_has(&vertexmap, *verts[j])) {
                    ARRLIST_vec4_add(&vertices, VertexReference(*verts[j]));
                    if (animated) ARRLIST_VertexSkin_add(&skins, geo->skins.data[original->skinstart + (*verts[j] - original->start)]);
                    HASHMAP_Index_set(&vertexmap, *verts[j], vertices.size - 1);
                }
                *(verts[j]) = HASHMAP_Index_get(&vertexmap, *verts[j]);
                if (*norms[j] != (uint32_t)-1) {
                    if (!HASHMAP_Index_has(&normalmap, *norms[j])) {
                        ARRLIST_vec3_add(&normals, NormalReference(*norms[j]));
                        HASHMAP_Index_set(&normalmap, *norms[j], normals.size - 1);
                    }
                    *(norms[j]) = HASHMAP_Index_get(&normalmap, *norms[j]);
                }
                if (*texs[j] != NO_UV) {
                    if (!HASHMAP_Index_has(&uvmap, *texs[j])) {
                        ARRLIST_vec2_add(&uvs, UVReference(*texs[j]));
                        HASHMAP_Index_set(&uvmap, *texs[j], uvs.size - 1);
                    }
                    *(texs[j]) = HASHMAP_Index_get(&uvmap, *texs[j]);
                }
            }
            if (!HASHMAP_Index_has(&materialmap, t.material)) {
                SurfaceMaterial copy; // byte copy: heap materials are only 8 byte aligned
                memcpy((void*)&copy, (const void*)MaterialReference(t.material), sizeof(SurfaceMaterial));
                ARRLIST_SurfaceMaterial_add(&materials, copy);
                ARRLIST_DynamicString_add(&materialnames, MaterialName(t.material));
                HASHMAP_Index_set(&materialmap, t.material, materials.size - 1);
            }
            t.material = HASHMAP_Index_get(&materialmap, t.material);
            ARRLIST_Triangle_add(&triangles, t);
        }
    }
    if (has_splats)
        for (size_t i = original->sstart; i <= original->send; i++)
            ARRLIST_GaussianSplat_add(&splats, *SplatReference(i));
    copy.start = 0;
    copy.end = (VertexID)vertices.size - 1;
    copy.tstart = 0;
    copy.tend = (TriangleID)triangles.size - 1;
    copy.sstart = 0;
    copy.send = (uint32_t)splats.size - 1;
    copy.skinstart = 0;
    copy.pose = animated ? 0 : (uint32_t)-1;
    ARRLIST_uint32_t texhandles = { 0 };
    for (size_t i = 0; i < materials.size; i++) PrismCollectMaterialTextures(&texhandles, &(materials.data[i]));
    PrismWrite(s, &copy, sizeof(copy));
    PRISM_WRITE_LIST(s, triangles);
    PRISM_WRITE_LIST(s, vertices);
    PRISM_WRITE_LIST(s, normals);
    PRISM_WRITE_LIST(s, uvs);
    PRISM_WRITE_LIST(s, materials);
    PrismWriteNames(s, &materialnames);
    PRISM_WRITE_LIST(s, splats);
    ARRLIST_MeshAnimation anims = { 0 };
    MeshAnimation* anim = animated ? AnimationReference(original->pose / MAX_BONES) : NULL;
    size_t meshid = 0;
    if (anim) {
        meshid = anim->meshid;
        anim->meshid = 0;
        anims.data = anim;
        anims.size = 1;
        anims.maxsize = 1;
    }
    PrismWriteAnimations(s, &anims);
    if (anim) anim->meshid = meshid;
    PRISM_WRITE_LIST(s, skins);
    PrismWriteSize(s, animated ? MAX_BONES : 0);
    if (animated) PrismWrite(s, &geo->poses.data[original->pose], MAX_BONES * sizeof(mat4));
    size_t sss_words = 0;
    uint32_t* sss = NULL;
    SubsurfaceCloudInfo info;
    if (QuerySubsurfaceCloudInfo(id, &info)) {
        if (HASHMAP_Index_has(&materialmap, info.material)) sss = SubsurfaceSerializeMesh(id, HASHMAP_Index_get(&materialmap, info.material), &sss_words);
        else logwarn("The subsurface cloud of object %zu uses a material the object no longer has, it was not saved", id);
    }
    PrismWriteSize(s, sss_words);
    PrismWrite(s, sss, sss_words * sizeof(uint32_t));
    if (sss) EZ_FREE(sss);
    PrismWriteTextureList(s, texhandles.data, texhandles.size);
    ARRLIST_uint32_t_clear(&texhandles);
    ARRLIST_Triangle_clear(&triangles);
    ARRLIST_vec4_clear(&vertices);
    ARRLIST_VertexSkin_clear(&skins);
    ARRLIST_vec3_clear(&normals);
    ARRLIST_vec2_clear(&uvs);
    ARRLIST_SurfaceMaterial_clear(&materials);
    ARRLIST_DynamicString_clear(&materialnames);
    ARRLIST_GaussianSplat_clear(&splats);
    HASHMAP_Index_clear(&vertexmap);
    HASHMAP_Index_clear(&normalmap);
    HASHMAP_Index_clear(&uvmap);
    HASHMAP_Index_clear(&materialmap);
}

static void PbjReadMesh(PrismStream* s, PBJMesh* m) {
    PrismRead(s, &m->md, sizeof(m->md));
    PRISM_READ_LIST(s, m->triangles);
    PRISM_READ_LIST(s, m->vertices);
    PRISM_READ_LIST(s, m->normals);
    PRISM_READ_LIST(s, m->uvs);
    PRISM_READ_LIST(s, m->materials);
    PrismReadNames(s, &m->materialnames, MAX_MATERIAL_NAME_SIZE);
    PRISM_READ_LIST(s, m->splats);
    PrismReadAnimations(s, &m->anims);
    PRISM_READ_LIST(s, m->skins);
    PRISM_READ_LIST(s, m->pose);
    m->sss_words = PrismReadSize(s);
    m->sss = PrismReadElements(s, m->sss_words, sizeof(uint32_t));
    if (!m->sss) m->sss_words = 0;
}

static const char* PbjMeshProblem(const PBJMesh* m) {
    for (size_t i = 0; i < m->triangles.size; i++) {
        const Triangle* t = &m->triangles.data[i];
        if (t->a >= m->vertices.size || t->b >= m->vertices.size || t->c >= m->vertices.size ||
            (t->an != (uint32_t)-1 && t->an >= m->normals.size) ||
            (t->bn != (uint32_t)-1 && t->bn >= m->normals.size) ||
            (t->cn != (uint32_t)-1 && t->cn >= m->normals.size) ||
            (t->at != NO_UV && t->at >= m->uvs.size) ||
            (t->bt != NO_UV && t->bt >= m->uvs.size) ||
            (t->ct != NO_UV && t->ct >= m->uvs.size) ||
            t->material >= m->materials.size)
            return "a triangle references data outside the file";
    }
    if (m->materialnames.size != m->materials.size) return "the material names do not match the materials";
    if (m->triangles.size == 0 && m->splats.size == 0) return "the object has no triangles or splats";
    return NULL;
}

static const char* PbjAnimationProblem(const PBJMesh* m) {
    if (m->anims.size != 1) return "it does not have exactly one animation set";
    MeshAnimation a; // local copy: the heap array is not aligned for Skeleton's mat4s
    memcpy(&a, &m->anims.data[0], sizeof(a));
    if (m->skins.size != m->vertices.size) return "the skin count does not match the vertex count";
    if (m->pose.size != MAX_BONES) return "the pose block is the wrong size";
    if (a.skeleton.bonecount == 0 || a.skeleton.bonecount > MAX_BONES) return "the skeleton has an invalid bone count";
    for (size_t b = 0; b < a.skeleton.bonecount; b++) {
        // every bone must reach a root within bonecount steps, so the hierarchy has no cycles
        size_t p = a.skeleton.bones[b].parent, steps = 0;
        while (p != (size_t)-1) {
            if (p >= a.skeleton.bonecount || ++steps > a.skeleton.bonecount) return "the skeleton hierarchy is invalid";
            p = a.skeleton.bones[p].parent;
        }
    }
    for (size_t i = 0; i < m->skins.size; i++) {
        float total = 0.0f;
        for (size_t k = 0; k < MAX_BONE_INFLUENCES; k++) {
            if (m->skins.data[i].indices[k] >= MAX_BONES) return "a vertex is bound to a bone outside the pose block";
            total += m->skins.data[i].weights[k];
        }
        if (!(total > 0.0f)) return "a vertex has no skin weight";
    }
    return NULL;
}

static size_t PbjSubmitMesh(PBJMesh* m, const char* name, BOOL animated) {
    VertexID startv = (VertexID)NumVertices();
    uint32_t startn = (uint32_t)NumNormals();
    uint32_t startu = (uint32_t)NumUVs();
    MaterialID startm = (MaterialID)NumMaterials();
    TriangleID startt = (TriangleID)NumTriangles();
    uint32_t startsp = (uint32_t)NumSplats();
    VertexID startsk = (VertexID)NumSkins();
    for (size_t i = 0; i < m->vertices.size; i++) SubmitVertex(m->vertices.data[i]);
    for (size_t i = 0; i < m->normals.size; i++) SubmitNormal(m->normals.data[i]);
    for (size_t i = 0; i < m->uvs.size; i++) SubmitUV(m->uvs.data[i]);
    for (size_t i = 0; i < m->materials.size; i++) {
        SurfaceMaterial copy; // byte copy: heap materials are only 8 byte aligned
        memcpy((void*)&copy, (const void*)&(m->materials.data[i]), sizeof(SurfaceMaterial));
        SubmitNamedMaterial(copy, m->materialnames.data[i] ? m->materialnames.data[i] : "Material");
    }
    for (size_t i = 0; i < m->triangles.size; i++) {
        Triangle t = m->triangles.data[i];
        t.a += startv;
        t.b += startv;
        t.c += startv;
        if (t.an != (uint32_t)-1) t.an += startn;
        if (t.bn != (uint32_t)-1) t.bn += startn;
        if (t.cn != (uint32_t)-1) t.cn += startn;
        if (t.at != NO_UV) t.at += startu;
        if (t.bt != NO_UV) t.bt += startu;
        if (t.ct != NO_UV) t.ct += startu;
        t.material += startm;
        SubmitTriangle(t);
    }
    for (size_t i = 0; i < m->splats.size; i++) SubmitSplat(m->splats.data[i]);
    if (animated) for (size_t i = 0; i < m->skins.size; i++) SubmitVertexSkin(m->skins.data[i]);
    MeshDescriptor md = m->md;
    md.start = startv;
    md.end = startv + (VertexID)m->vertices.size - 1;
    md.tstart = startt;
    md.tend = startt + (TriangleID)m->triangles.size - 1;
    md.sstart = startsp;
    md.send = startsp + (uint32_t)m->splats.size - 1;
    md.skinstart = startsk;
    md.pose = (uint32_t)-1;
    SubmitMeshDescriptor(md, name);
    size_t mesh = NumMeshes() - 1;
    if (animated) {
        // heap copy: a MeshAnimation is too large to keep on the stack
        MeshAnimation* src = EZ_ALLOC(1, sizeof(MeshAnimation));
        memcpy((void*)src, (const void*)&m->anims.data[0], sizeof(MeshAnimation));
        size_t aidx = SubmitRig(mesh, &(src->skeleton));
        MeshAnimation* dst = AnimationReference(aidx);
        for (size_t j = 0; j < src->animations.size; j++) {
            Animation clip = src->animations.data[j];
            if (j < src->names.size && src->names.data[j]) {
                strncpy(clip.name, src->names.data[j], sizeof(clip.name) - 1);
                clip.name[sizeof(clip.name) - 1] = '\0';
            }
            char* clipname = EZ_ALLOC(strlen(clip.name) + 1, sizeof(char));
            strcpy(clipname, clip.name);
            ARRLIST_Animation_add(&(dst->animations), clip);
            ARRLIST_DynamicString_add(&(dst->names), clipname);
            src->animations.data[j].channels = (ARRLIST_BoneChannel){ 0 }; // the scene owns the keyframes now
        }
        dst->current = src->current < dst->animations.size ? src->current : 0;
        dst->time = src->time;
        dst->looping = src->looping;
        dst->playing = src->playing;
        dst->enable = src->enable;
        dst->_enable = src->_enable;
        dst->synced = src->synced;
        dst->blending = FALSE;
        dst->rest = FALSE;
        dst->dirty = TRUE;
        memcpy(dst->posed, src->posed, sizeof(dst->posed));
        memcpy((void*)dst->overrides, (const void*)src->overrides, sizeof(dst->overrides));
        memcpy((void*)dst->ik, (const void*)src->ik, sizeof(dst->ik));
        dst->numik = src->numik <= MAX_IK_CONSTRAINTS ? src->numik : 0;
        EZ_FREE(src);
        MeshReference(mesh)->pose = (uint32_t)(aidx * MAX_BONES);
        memcpy(&RendererGeometry()->poses.data[aidx * MAX_BONES], m->pose.data, MAX_BONES * sizeof(mat4)); // pose as saved
        UpdatePoses();
        UpdateMeshes();
    }
    if (m->sss_words > 0) SubsurfaceDeserializeMesh(m->sss, m->sss_words, mesh, startm);
    return mesh;
}

const char* GetOpenFile(const char* type) {
    nfdchar_t* outpath = NULL;
    nfdresult_t result = NFD_OpenDialog(type, NULL, &outpath);
    if (result == NFD_CANCEL) {
        logtrace("Open file cancelled by user");
    } else if (result != NFD_OKAY) {
        logerror("Unable to open file due to NFD error: %s", NFD_GetError());
    }
    return (char*)outpath;
}

const char* GetSaveFile(const char* type) {
    nfdchar_t* outpath = NULL;
    nfdresult_t result = NFD_SaveDialog(type, NULL, &outpath);
    if (result == NFD_CANCEL) {
        logtrace("Save file cancelled by user");
    } else if (result != NFD_OKAY) {
        logerror("Unable to save file due to NFD error: %s", NFD_GetError());
    }
    return (char*)outpath;
}

BOOL LoadOBJ(const char* filepath) {
    ez_File* file = ez_load_file(filepath);
    VertexID startv = NumVertices();
    TriangleID startt = NumTriangles();
    if (!file) {
        logerror("Unable to load invalid filepath \"%s\"", filepath);
        return FALSE;
    }
    if (file->type != DOTOBJ) {
        logerror("\"%s\" is not a .obj file. Unable to open it with the OBJ loader", filepath);
        ez_free_file(file);
        return FALSE;
    }
    ez_FileParser parser = ez_parser(file);
    StateOBJ state = { 0 };
    state.filepath = filepath;
    char line[MAX_OBJ_LINE_SIZE] = { 0 };
    while (ez_next_line(&parser, line, MAX_OBJ_LINE_SIZE)) {
        char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE] = { 0 };
        size_t numargs = ParseLineArgsOBJ(line, lineargs);
        if (numargs > 0 && lineargs[0][0] != '#') {
            ParseFuncOBJ p = GetParserFromArgOBJ(lineargs[0]);
            if (p) { if (!p(lineargs, numargs, &state)) { logerror("%s:%d - Unable to parse this OBJ field due to an error", filepath, (int)parser.line); CleanStateOBJ(&state); ez_free_file(file); return FALSE; }
            } else logwarn("%s:%d - Unknown OBJ property detected: \"%s\", skipping parsing this field...", filepath, (int)parser.line, lineargs[0]);
        }
    }
    if (!ConstructOBJ(state)) {
        logerror("Unable to construct .obj \"%s\" due to an error", filepath);
        ez_free_file(file);
        return FALSE;
    } else {
        vec3 min, max, center, extent;
        glm_vec3_copy(state.vertices.data[0], min);
        glm_vec3_copy(state.vertices.data[0], max);
        for (size_t i = 1; i < state.vertices.size; i++) {
            glm_vec3_minv(state.vertices.data[i], min, min);
            glm_vec3_maxv(state.vertices.data[i], max, max);
        }
        glm_vec3_add(max, min, center);
        glm_vec3_scale(center, 0.5f, center);
        glm_vec3_sub(max, min, extent);
        glm_vec3_scale(extent, 0.5f, extent);
        SubmitMeshDescriptor((MeshDescriptor){
            FALSE, startv, NumVertices() - 1, startt, NumTriangles() - 1, 0, (uint32_t)-1,
            (uint32_t)NumSplats(), (uint32_t)NumSplats() - 1, INLINEV3(center), INLINEV3(extent), { 0 }, { 0 },
            { 1.0f, 1.0f, 1.0f }, GLM_MAT4_IDENTITY_INIT, TEXFILTER_BILINEAR, FALSE, 1 }, ez_strip_filename(filepath));
    }
    CleanStateOBJ(&state);
    ez_free_file(file);
    return TRUE;
}

static void LoadFBXTexture(const struct aiMaterial* mat, enum aiTextureType type, const char* dir, SurfaceMaterial* m, int slot, BOOL color, BOOL normal) {
    if (m->textures[slot] != 0 || aiGetMaterialTextureCount(mat, type) == 0) return;
    struct aiString texpath;
    if (aiGetMaterialTexture(mat, type, 0, &texpath, NULL, NULL, NULL, NULL, NULL, NULL) != AI_SUCCESS) return;
    if (texpath.data[0] == '*') {
        logwarn("Embedded FBX texture \"%s\" is not supported, extract it next to the file to use it", texpath.data);
        return;
    }
    char name[MAX_TEXTURE_PATH_SIZE] = { 0 };
    strncpy(name, texpath.data, sizeof(name) - 1);
    for (char* c = name; *c; c++) if (*c == '\\') *c = '/';
    char path[MAX_TEXTURE_PATH_SIZE] = { 0 };
    BOOL absolute = name[0] == '/' || (name[0] != 0 && name[1] == ':');
    snprintf(path, sizeof(path), "%s%s", absolute ? "" : dir, name);
    size_t before = NumTextures();
    size_t tex = LoadTextureFile(path, TRUE);
    const char* basename = ez_strip_filename(name);
    if (tex == (size_t)-1 && absolute && basename) {
        snprintf(path, sizeof(path), "%s%s", dir, basename);
        tex = LoadTextureFile(path, TRUE);
    }
    if (tex == (size_t)-1) {
        logwarn("Unable to load FBX texture \"%s\", the material keeps its plain values", texpath.data);
        return;
    }
    if (NumTextures() > before) {
        if (!color) TextureReference(tex)->settings.srgb = FALSE;
        if (normal) TextureReference(tex)->settings.normalmap = TRUE;
    }
    m->textures[slot] = TextureHandleOf(tex);
}

static MaterialID LoadFBXMaterial(const struct aiMaterial* mat, const char* dir) {
    SurfaceMaterial m = { 0 };
    struct aiColor4D color;
    float fval;
    int ival;
    if (aiGetMaterialColor(mat, AI_MATKEY_COLOR_DIFFUSE, &color) == AI_SUCCESS) _aic2v(_4d23d(color), m.diffuse);
    if (aiGetMaterialColor(mat, AI_MATKEY_COLOR_SPECULAR, &color) == AI_SUCCESS) _aic2v(_4d23d(color), m.specular);
    if (aiGetMaterialColor(mat, AI_MATKEY_COLOR_AMBIENT, &color) == AI_SUCCESS) _aic2v(_4d23d(color), m.ambient);
    if (aiGetMaterialColor(mat, AI_MATKEY_COLOR_EMISSIVE, &color) == AI_SUCCESS) _aic2v(_4d23d(color), m.emission);
    if (aiGetMaterialColor(mat, AI_MATKEY_COLOR_TRANSPARENT, &color) == AI_SUCCESS) _aic2v(_4d23d(color), m.absorbtion);
    if (aiGetMaterialFloat(mat, AI_MATKEY_SHININESS, &fval) == AI_SUCCESS) m.shiny = fval;
    if (aiGetMaterialFloat(mat, AI_MATKEY_REFRACTI, &fval) == AI_SUCCESS) m.ior = fval;
    if (aiGetMaterialInteger(mat, AI_MATKEY_SHADING_MODEL, &ival) == AI_SUCCESS) {
        switch (ival) {
            case aiShadingMode_Phong:
            case aiShadingMode_Blinn: m.model = 2; break;
            case aiShadingMode_CookTorrance:
            case aiShadingMode_Fresnel: m.model = 7; break;
            default: m.model = 2; break;
        }
    } else {
        m.model = 2;
    }
    LoadFBXTexture(mat, aiTextureType_DIFFUSE, dir, &m, TEXSLOT_DIFFUSE, TRUE, FALSE);
    LoadFBXTexture(mat, aiTextureType_BASE_COLOR, dir, &m, TEXSLOT_DIFFUSE, TRUE, FALSE);
    LoadFBXTexture(mat, aiTextureType_SPECULAR, dir, &m, TEXSLOT_SPECULAR, TRUE, FALSE);
    LoadFBXTexture(mat, aiTextureType_EMISSIVE, dir, &m, TEXSLOT_EMISSION, TRUE, FALSE);
    LoadFBXTexture(mat, aiTextureType_AMBIENT, dir, &m, TEXSLOT_AMBIENT, TRUE, FALSE);
    LoadFBXTexture(mat, aiTextureType_SHININESS, dir, &m, TEXSLOT_SHINY, FALSE, FALSE);
    LoadFBXTexture(mat, aiTextureType_NORMALS, dir, &m, TEXSLOT_NORMAL, FALSE, TRUE);
    LoadFBXTexture(mat, aiTextureType_NORMAL_CAMERA, dir, &m, TEXSLOT_NORMAL, FALSE, TRUE);
    float* bases[] = { m.diffuse, m.specular, m.emission, m.ambient };
    int slots[] = { TEXSLOT_DIFFUSE, TEXSLOT_SPECULAR, TEXSLOT_EMISSION, TEXSLOT_AMBIENT };
    for (int k = 0; k < 4; k++) // a map multiplies the plain value, so a map given without the matching color would vanish
        if (m.textures[slots[k]] && bases[k][0] == 0.0f && bases[k][1] == 0.0f && bases[k][2] == 0.0f)
            bases[k][0] = bases[k][1] = bases[k][2] = 1.0f;
    if (m.textures[TEXSLOT_SHINY] && m.shiny == 0.0f) m.shiny = 1.0f;
    struct aiString matname;
    const char* namestr = "Untitled FBXMaterial";
    if (aiGetMaterialString(mat, AI_MATKEY_NAME, &matname) == AI_SUCCESS) namestr = matname.data;
    return SubmitNamedMaterial(m, namestr);
}

static uint32_t LookupOrRegisterBone(Skeleton* sk, const char* name, const struct aiMatrix4x4* offset) {
    for (size_t i = 0; i < sk->bonecount; i++) {
        if (strcmp(sk->bones[i].name, name) == 0)
            return (uint32_t)i;
    }
    if (sk->bonecount >= MAX_BONES) {
        logwarn("Skeleton exceeds MAX_BONES (%d) — bone \"%s\" skipped", MAX_BONES, name);
        return 0;
    }
    Bone* b = &sk->bones[sk->bonecount];
    strncpy(b->name, name, sizeof(b->name) - 1);
    struct aiMatrix4x4 t = *offset;
    aiTransposeMatrix4(&t);
    memcpy(b->inversebind, &t, sizeof(mat4));
    b->parent = (size_t)-1;
    return (uint32_t)(sk->bonecount++);
}

static BOOL LoadFBXMesh(Skeleton* skeleton, const struct aiMesh* mesh, MaterialID mat_id, size_t vertices_start, size_t normals_start, size_t uvs_start) {
    if (!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)) {
        logwarn("Non-triangle FBX mesh detected - skipping \"%s\"", mesh->mName.data);
        return TRUE;
    }
    BOOL has_normals = mesh->mNormals != NULL;
    const struct aiVector3D* uvs = mesh->mTextureCoords[0];
    for (size_t i = 0; i < mesh->mNumVertices; i++) {
        vec3 v;
        _aiv32v3(mesh->mVertices[i], v);
        SubmitVertex(v);
    }
    VertexSkin* skins = EZ_ALLOC(mesh->mNumVertices, sizeof(VertexSkin));
    uint32_t* influence_count = EZ_ALLOC(mesh->mNumVertices, sizeof(uint32_t));
    for (size_t b = 0; b < mesh->mNumBones; b++) {
        struct aiBone* bone = mesh->mBones[b];
        uint32_t bone_idx = LookupOrRegisterBone(skeleton, bone->mName.data, &bone->mOffsetMatrix);
        for (size_t w = 0; w < bone->mNumWeights; w++) {
            uint32_t vi = bone->mWeights[w].mVertexId;
            uint32_t slot = influence_count[vi];
            if (slot < MAX_BONE_INFLUENCES) {
                skins[vi].indices[slot] = bone_idx;
                skins[vi].weights[slot] = bone->mWeights[w].mWeight;
                influence_count[vi]++;
            }
        }
    }
    for (size_t i = 0; i < mesh->mNumVertices; i++) SubmitVertexSkin(skins[i]);
    EZ_FREE(skins);
    EZ_FREE(influence_count);
    if (has_normals) {
        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            vec3 n;
            _aiv32v3(mesh->mNormals[i], n);
            SubmitNormal(n);
        }
    }
    if (uvs) for (unsigned int i = 0; i < mesh->mNumVertices; i++) SubmitUV((vec2){ uvs[i].x, uvs[i].y });
    for (size_t i = 0; i < mesh->mNumFaces; i++) {
        const struct aiFace* face = &mesh->mFaces[i];
        if (face->mNumIndices != 3) {
            logwarn("FBX face is a non-triangle - skipping");
            continue;
        }
        unsigned int ia = face->mIndices[0];
        unsigned int ib = face->mIndices[1];
        unsigned int ic = face->mIndices[2];
        Triangle tri = {
            vertices_start + ia,
            vertices_start + ib,
            vertices_start + ic,
            has_normals ? (normals_start + ia) : (VertexID)-1,
            has_normals ? (normals_start + ib) : (VertexID)-1,
            has_normals ? (normals_start + ic) : (VertexID)-1,
            mat_id,
            NO_UVS
        };
        if (uvs) {
            tri.at = (uint32_t)(uvs_start + ia);
            tri.bt = (uint32_t)(uvs_start + ib);
            tri.ct = (uint32_t)(uvs_start + ic);
        }
        SubmitTriangle(tri);
    }
    return TRUE;
}

static Animation* LoadFBXAnimations(const struct aiScene* scene, size_t* out_count) {
    *out_count = scene->mNumAnimations;
    if (scene->mNumAnimations == 0) return NULL;
    Animation* anims = EZ_ALLOC(scene->mNumAnimations, sizeof(Animation));
    for (size_t a = 0; a < scene->mNumAnimations; a++) {
        const struct aiAnimation* ai_anim = scene->mAnimations[a];
        Animation* anim = &anims[a];
        strncpy(anim->name, ai_anim->mName.data, sizeof(anim->name) - 1);
        anim->duration = ai_anim->mDuration;
        anim->tps = ai_anim->mTicksPerSecond > 0.0f ? ai_anim->mTicksPerSecond : 25.0f;
        ARRLIST_BoneChannel_zero(&anim->channels, ai_anim->mNumChannels); // upper bound
        anim->channels.size = 0;
        for (size_t c = 0; c < ai_anim->mNumChannels; c++) {
            const struct aiNodeAnim* ch = ai_anim->mChannels[c];
            char base[256];
            strncpy(base, ch->mNodeName.data, sizeof(base) - 1);
            base[sizeof(base) - 1] = '\0';
            char* suffix = strstr(base, "_$AssimpFbx$_");
            if (suffix) *suffix = '\0';
            BoneChannel* bone = NULL;
            for (size_t i = 0; i < anim->channels.size; i++) {
                if (strcmp(anim->channels.data[i].name, base) == 0) {
                    bone = &anim->channels.data[i];
                    break;
                }
            }
            if (!bone) {
                BoneChannel new_ch = { 0 };
                strncpy(new_ch.name, base, sizeof(new_ch.name) - 1);
                ARRLIST_BoneChannel_add(&anim->channels, new_ch);
                bone = &anim->channels.data[anim->channels.size - 1];
            }
            if (ch->mNumPositionKeys > bone->positions.size) {
                ARRLIST_Vec3Key_clear(&bone->positions);
                ARRLIST_Vec3Key_zero(&bone->positions, ch->mNumPositionKeys);
                for (unsigned int k = 0; k < ch->mNumPositionKeys; k++) {
                    bone->positions.data[k].time = (float)ch->mPositionKeys[k].mTime;
                    _aiv32v3(ch->mPositionKeys[k].mValue, bone->positions.data[k].value);
                }
            }
            if (ch->mNumRotationKeys > bone->rotations.size) {
                ARRLIST_QuatKey_clear(&bone->rotations);
                ARRLIST_QuatKey_zero(&bone->rotations, ch->mNumRotationKeys);
                for (unsigned int k = 0; k < ch->mNumRotationKeys; k++) {
                    bone->rotations.data[k].time = (float)ch->mRotationKeys[k].mTime;
                    _aiq2v(ch->mRotationKeys[k].mValue, bone->rotations.data[k].value);
                }
            }
            if (ch->mNumScalingKeys > bone->scales.size) {
                ARRLIST_Vec3Key_clear(&bone->scales);
                ARRLIST_Vec3Key_zero(&bone->scales, ch->mNumScalingKeys);
                for (unsigned int k = 0; k < ch->mNumScalingKeys; k++) {
                    bone->scales.data[k].time = (float)ch->mScalingKeys[k].mTime;
                    _aiv32v3(ch->mScalingKeys[k].mValue, bone->scales.data[k].value);
                }
            }
        }
    }
    return anims;
}

static size_t FindBoneIndex(Skeleton* skeleton, const char* name) {
    for (size_t i = 0; i < skeleton->bonecount; i++)
        if (strcmp(skeleton->bones[i].name, name) == 0) return i;
    return (size_t)-1;
}

static void ResolveParents(Skeleton* skeleton, const struct aiNode* node, size_t parent_bone_idx, struct aiMatrix4x4 parent_accum) {
    size_t my_idx = FindBoneIndex(skeleton, node->mName.data);
    int is_pivot = strstr(node->mName.data, "_$AssimpFbx$_") != NULL;
    struct aiMatrix4x4 accum = parent_accum;
    if (is_pivot)
        aiMultiplyMatrix4(&accum, &node->mTransformation);

    if (my_idx != (size_t)-1) {
        skeleton->bones[my_idx].parent = parent_bone_idx;
        struct aiMatrix4x4 full = accum;
        aiMultiplyMatrix4(&full, &node->mTransformation);
        struct aiMatrix4x4 t = full;
        aiTransposeMatrix4(&t);
        memcpy(skeleton->bones[my_idx].localbind, &t, sizeof(mat4));
        struct aiMatrix4x4 identity;
        aiIdentityMatrix4(&identity);
        for (size_t i = 0; i < node->mNumChildren; i++)
            ResolveParents(skeleton, node->mChildren[i], my_idx, identity);
    } else {
        for (size_t i = 0; i < node->mNumChildren; i++)
            ResolveParents(skeleton, node->mChildren[i], parent_bone_idx, accum);
    }
}

static BOOL TraverseFBXNode(Skeleton* skeleton, const struct aiNode* node, const struct aiScene* scene, MaterialID* mat_ids, vec3* aabb_min, vec3* aabb_max, BOOL* aabb_init) {
    for (unsigned int m = 0; m < node->mNumMeshes; m++) {
        unsigned int mesh_idx = node->mMeshes[m];
        const struct aiMesh* mesh = scene->mMeshes[mesh_idx];
        MaterialID mat_id = mat_ids[mesh->mMaterialIndex];
        size_t vstart = NumVertices();
        size_t nstart = NumNormals();
        size_t ustart = NumUVs();
        if (!LoadFBXMesh(skeleton, mesh, mat_id, vstart, nstart, ustart))
            return FALSE;
        for (unsigned int v = 0; v < mesh->mNumVertices; v++) {
            vec3 vtx;
            _aiv32v3(mesh->mVertices[v], vtx);
            if (!(*aabb_init)) {
                glm_vec3_copy(vtx, *aabb_min);
                glm_vec3_copy(vtx, *aabb_max);
                *aabb_init = TRUE;
            } else {
                glm_vec3_minv(vtx, *aabb_min, *aabb_min);
                glm_vec3_maxv(vtx, *aabb_max, *aabb_max);
            }
        }
    }
    for (unsigned int c = 0; c < node->mNumChildren; c++) {
        if (!TraverseFBXNode(skeleton, node->mChildren[c], scene, mat_ids, aabb_min, aabb_max, aabb_init))
            return FALSE;
    }
    return TRUE;
}

BOOL LoadFBX(const char* filepath) {
    unsigned int flags = aiProcess_Triangulate          // ensure all faces are tris
                       | aiProcess_GenSmoothNormals     // generate normals if missing
                       | aiProcess_JoinIdenticalVertices
                       | aiProcess_LimitBoneWeights     // cap bone influences per vertex
                       | aiProcess_PopulateArmatureData; // reconstruct skeleton hierarchy, converting world-space animation channels to local-space
    const struct aiScene* scene = aiImportFile(filepath, flags);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        logerror("Failed to load FBX file \"%s\": %s", filepath, aiGetErrorString());
        return FALSE;
    }
    uint32_t starts = NumSkins();
    VertexID startv = NumVertices();
    TriangleID startt = NumTriangles();
    MaterialID* mat_ids = EZ_ALLOC(scene->mNumMaterials, sizeof(MaterialID));
    char fbxdir[MAX_TEXTURE_PATH_SIZE] = { 0 };
    strncpy(fbxdir, filepath, sizeof(fbxdir) - 1);
    char* fbxname = (char*)ez_strip_filename(fbxdir);
    if (fbxname) fbxname[0] = 0;
    else fbxdir[0] = 0;
    for (unsigned int i = 0; i < scene->mNumMaterials; i++) mat_ids[i] = LoadFBXMaterial(scene->mMaterials[i], fbxdir); // uvs stay in the OBJ convention (v up)
    vec3 aabb_min = GLM_VEC3_ZERO_INIT;
    vec3 aabb_max = GLM_VEC3_ZERO_INIT;
    BOOL aabb_init = FALSE;
    Skeleton skeleton = { 0 };
    if (!TraverseFBXNode(&skeleton, scene->mRootNode, scene, mat_ids, &aabb_min, &aabb_max, &aabb_init)) {
        logerror("Failed to traverse scene graph for \"%s\"", filepath);
        EZ_FREE(mat_ids);
        aiReleaseImport(scene);
        return FALSE;
    }
    struct aiMatrix4x4 identity;
    aiIdentityMatrix4(&identity);
    ResolveParents(&skeleton, scene->mRootNode, (size_t)-1, identity);
    EZ_FREE(mat_ids);
    if (aabb_init) {
        vec3 center, extent;
        glm_vec3_add(aabb_max, aabb_min, center);
        glm_vec3_scale(center, 0.5f, center);
        glm_vec3_sub(aabb_max, aabb_min, extent);
        glm_vec3_scale(extent, 0.5f, extent);
        SubmitMeshDescriptor((MeshDescriptor){
            FALSE, startv, NumVertices() - 1,
            startt, NumTriangles() - 1,
            starts, (uint32_t)-1,
            (uint32_t)NumSplats(), (uint32_t)NumSplats() - 1,
            INLINEV3(center), INLINEV3(extent),
            { 0 }, { 0 },
            { 1.0f, 1.0f, 1.0f }, GLM_MAT4_IDENTITY_INIT, TEXFILTER_BILINEAR, FALSE, 1
        }, ez_strip_filename(filepath));
        size_t num_anims = 0;
        Animation* anims = LoadFBXAnimations(scene, &num_anims);
        for (size_t i = 0; i < num_anims; i++) SubmitAnimation(NumMeshes() - 1, skeleton, anims[i]);
        EZ_FREE(anims);
        if (num_anims > 0) {
            MeshReference(NumMeshes() - 1)->pose = (NumAnimations() - 1) * MAX_BONES;
        } else if (skeleton.bonecount > 0) {
            for (size_t b = 0; b < skeleton.bonecount; b++) {
                mat4 inv, bind, local;
                memcpy(inv, skeleton.bones[b].inversebind, sizeof(mat4));
                glm_mat4_inv(inv, bind);
                size_t p = skeleton.bones[b].parent;
                if (p != (size_t)-1 && p < skeleton.bonecount && p != b) {
                    mat4 pinv;
                    memcpy(pinv, skeleton.bones[p].inversebind, sizeof(mat4));
                    glm_mat4_mul(pinv, bind, local);
                } else {
                    memcpy(local, bind, sizeof(mat4));
                }
                memcpy(skeleton.bones[b].localbind, local, sizeof(mat4));
            }
            size_t aidx = SubmitRig(NumMeshes() - 1, &skeleton);
            AnimationReference(aidx)->synced = TRUE;
            AnimationReference(aidx)->looping = FALSE;
            MeshReference(NumMeshes() - 1)->pose = (uint32_t)(aidx * MAX_BONES);
            loginfo("\"%s\" has a %zu bone skeleton without animations - it is ready to pose and animate", filepath, skeleton.bonecount);
        }
    }
    aiReleaseImport(scene);
    return TRUE;
}

BOOL LoadPLY(const char* filepath) {
    if (!filepath) {
        logerror("Cannot load Gaussian PLY from a NULL filepath");
        return FALSE;
    }
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        logerror("Unable to open Gaussian PLY \"%s\": %s", filepath, strerror(errno));
        return FALSE;
    }
    PLYHeader header = { 0 };
    if (!ParsePLYHeader(file, &header)) {
        fclose(file);
        return FALSE;
    }
    const int px = FindPLYProperty(&header, "x");
    const int py = FindPLYProperty(&header, "y");
    const int pz = FindPLYProperty(&header, "z");
    const int sx = FindPLYProperty(&header, "scale_0");
    const int sy = FindPLYProperty(&header, "scale_1");
    const int sz = FindPLYProperty(&header, "scale_2");
    const int r0 = FindPLYProperty(&header, "rot_0");
    const int r1 = FindPLYProperty(&header, "rot_1");
    const int r2 = FindPLYProperty(&header, "rot_2");
    const int r3 = FindPLYProperty(&header, "rot_3");
    const int opacity = FindPLYProperty(&header, "opacity");
    const int dc0 = FindPLYProperty(&header, "f_dc_0");
    const int dc1 = FindPLYProperty(&header, "f_dc_1");
    const int dc2 = FindPLYProperty(&header, "f_dc_2");
    if (px < 0 || py < 0 || pz < 0) {
        logerror("Gaussian PLY \"%s\" is missing position properties", filepath);
        fclose(file);
        return FALSE;
    }
    if (sx < 0 || sy < 0 || sz < 0) {
        logerror("Gaussian PLY \"%s\" is missing scale properties", filepath);
        fclose(file);
        return FALSE;
    }
    if (r0 < 0 || r1 < 0 || r2 < 0 || r3 < 0) {
        logerror("Gaussian PLY \"%s\" is missing rotation properties", filepath);
        fclose(file);
        return FALSE;
    }
    if (opacity < 0) {
        logerror("Gaussian PLY \"%s\" is missing opacity", filepath);
        fclose(file);
        return FALSE;
    }
    if (dc0 < 0 || dc1 < 0 || dc2 < 0) {
        logerror("Gaussian PLY \"%s\" is missing spherical harmonic DC coefficients", filepath);
        fclose(file);
        return FALSE;
    }
    ARRLIST_GaussianSplat splats = { 0 };
    for (size_t i = 0; i < header.vertex_count; i++) {
        GaussianSplat gaussian = { 0 };
        for (size_t property = 0; property < header.property_count; property++) {
            float value = ReadPLYFloat(file, header.properties[property].type);
            if (isnan(value)) {
                logerror("Unexpected end of data while reading Gaussian %zu in \"%s\"", i, filepath);
                ARRLIST_GaussianSplat_clear(&splats);
                fclose(file);
                return FALSE;
            }
            const char* name = header.properties[property].name;
            if (strcmp(name, "x") == 0) gaussian.position[0] = value;
            else if (strcmp(name, "y") == 0) gaussian.position[1] = -value;
            else if (strcmp(name, "z") == 0) gaussian.position[2] = value;
            else if (strcmp(name, "scale_0") == 0) gaussian.logscale[0] = value;
            else if (strcmp(name, "scale_1") == 0) gaussian.logscale[1] = value;
            else if (strcmp(name, "scale_2") == 0) gaussian.logscale[2] = value;
            else if (strcmp(name, "rot_0") == 0) gaussian.rotation[0] = value;
            else if (strcmp(name, "rot_1") == 0) gaussian.rotation[1] = -value;
            else if (strcmp(name, "rot_2") == 0) gaussian.rotation[2] = value;
            else if (strcmp(name, "rot_3") == 0) gaussian.rotation[3] = -value;
            else if (strcmp(name, "opacity") == 0) gaussian.opacity = value;
            else if (strcmp(name, "f_dc_0") == 0) gaussian.shdc[0] = value;
            else if (strcmp(name, "f_dc_1") == 0) gaussian.shdc[1] = value;
            else if (strcmp(name, "f_dc_2") == 0) gaussian.shdc[2] = value;
        }
        ARRLIST_GaussianSplat_add(&splats, gaussian);
    }
    fclose(file);
    size_t first = NumSplats();
    for (size_t i = 0; i < splats.size; i++) SubmitSplat(splats.data[i]);
    SubmitSplatMeshDescriptor(first, splats.size, ez_strip_filename(filepath));
    ARRLIST_GaussianSplat_clear(&splats);
    loginfo("Loaded %zu Gaussian splats from \"%s\"", header.vertex_count, filepath);
    return TRUE;
}

BOOL LoadPRISM(const char* filepath) {
    if (!filepath) {
        logerror("Cannot load from a NULL filepath");
        return FALSE;
    }
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        logerror("Unable to open PRISM file \"%s\": %s", filepath, strerror(errno));
        return FALSE;
    }
    PrismStream s = { file, TRUE, 0 };
    if (fseek(file, 0, SEEK_END) == 0) {
        long len = ftell(file);
        s.remaining = len > 0 ? (size_t)len : 0;
    }
    fseek(file, 0, SEEK_SET);
    char magic[4] = { 0 };
    uint32_t version = 0;
    uint64_t geomsize = 0;
    PrismRead(&s, magic, sizeof(magic));
    PrismRead(&s, &version, sizeof(version));
    PrismRead(&s, &geomsize, sizeof(geomsize));
    if (!s.ok || memcmp(magic, PRISM_MAGIC, 4) != 0) {
        fclose(file);
        logerror("\"%s\" is not a PRISM session file (or was written by an older, incompatible version)", filepath);
        return FALSE;
    }
    if (version != PRISM_VERSION || geomsize != sizeof(Geometry)) {
        fclose(file);
        logerror("PRISM file \"%s\" was written by an incompatible build (version %u, layout %llu; expected version %u, layout %llu)",
            filepath, version, (unsigned long long)geomsize, PRISM_VERSION, (unsigned long long)sizeof(Geometry));
        return FALSE;
    }
    Geometry loaded = { 0 };
    PrismReadGeometry(&s, &loaded);
    size_t sss_words = PrismReadSize(&s);
    uint32_t* sss = PrismReadElements(&s, sss_words, sizeof(uint32_t));
    PrismTextureSet textures = { 0 };
    PrismReadTextureSet(&s, &textures);
    fclose(file);
    if (!s.ok) {
        EZ_FREE(sss);
        PrismFreeTextureSet(&textures);
        PrismFreeGeometry(&loaded);
        logerror("PRISM file \"%s\" is truncated or corrupt", filepath);
        return FALSE;
    }
    ClearScene(TRUE);
    RigAutoWeightsCancel();
    RigSetActiveMesh((size_t)-1);
    Renderer* renderer = RawRendererRef();
    Geometry* live = &(renderer->geometry);
    PrismFreeGeometry(live);
    loaded.manifold = live->manifold;
    loaded.arap = live->arap;
    loaded.fluid = live->fluid;
    loaded.changes = live->changes;
    ARRLIST_uint32_t_clear(&(live->textures));
    *live = loaded;
    PrismSubmitTextureSet(&textures);
    PrismFreeTextureSet(&textures);
    UpdateNormals();
    UpdateUVs();
    UpdateVertices();
    UpdateMaterials();
    UpdateLights();
    UpdateTriangles();
    UpdateMeshes();
    UpdateSkins();
    UpdatePoses();
    UpdateSplats();
    SubsurfaceDeserialize(sss, sss ? sss_words : 0);
    EZ_FREE(sss);
    KeysChanged();
    for (size_t i = 0; i < NumAnimations(); i++) {
        MeshAnimation* anim = AnimationReference(i);
        anim->rest = FALSE;
        anim->blending = FALSE;
        anim->evalclip = (size_t)-1;
        anim->dirty = TRUE;
    }
    loginfo("Successfully loaded prism session from \"%s\"", filepath);
    return TRUE;
}

BOOL LoadPBJ(const char* filepath, PBJType* loaded_type, size_t* loaded_index) {
    if (!filepath) {
        logerror("Cannot load from a NULL filepath");
        return FALSE;
    }
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        logerror("Unable to open PBJ file \"%s\": %s", filepath, strerror(errno));
        return FALSE;
    }
    PrismStream s = { file, TRUE, 0 };
    if (fseek(file, 0, SEEK_END) == 0) {
        long len = ftell(file);
        s.remaining = len > 0 ? (size_t)len : 0;
    }
    fseek(file, 0, SEEK_SET);
    char magic[3] = { 0 };
    uint32_t version = 0;
    PrismRead(&s, magic, sizeof(magic));
    PrismRead(&s, &version, sizeof(version));
    if (!s.ok || memcmp(magic, PBJ_MAGIC, 3) != 0) {
        fclose(file);
        logerror("\"%s\" is not a PBJ file", filepath);
        return FALSE;
    }
    if (version != PBJ_VERSION) {
        fclose(file);
        logerror("PBJ file \"%s\" was written by an incompatible build (version %u, expected %u)", filepath, version, PBJ_VERSION);
        return FALSE;
    }
    uint32_t tag = (uint32_t)-1;
    PrismRead(&s, &tag, sizeof(tag));
    if (!s.ok || tag >= PBJ_TYPE_COUNT) {
        fclose(file);
        logerror("PBJ file \"%s\" holds an unknown item type", filepath);
        return FALSE;
    }
    PBJType type = (PBJType)tag;
    char* name = PrismReadString(&s, PBJ_MAX_NAME, 0);
    SceneCamera camera = { 0 };
    SurfaceMaterial material = { 0 };
    SceneLight light = { 0 };
    FluidForce force = { 0 };
    FluidSource source = { 0 };
    PBJMesh mesh = { 0 };
    PrismTextureSet textures = { 0 };
    SceneTexture texture = { 0 };
    char* texname = NULL;
    switch (type) {
        case PBJ_CAMERA: PrismRead(&s, &camera, sizeof(camera)); break;
        case PBJ_MATERIAL: PrismRead(&s, &material, sizeof(material)); PrismReadTextureSet(&s, &textures); break;
        case PBJ_LIGHT: PrismRead(&s, &light, sizeof(light)); PrismReadTextureSet(&s, &textures); break;
        case PBJ_MESH: PbjReadMesh(&s, &mesh); PrismReadTextureSet(&s, &textures); break;
        case PBJ_FORCE: PrismRead(&s, &force, sizeof(force)); break;
        case PBJ_SOURCE: PrismRead(&s, &source, sizeof(source)); break;
        case PBJ_TEXTURE: PrismReadTexture(&s, &texture, &texname); break;
        default: break;
    }
    if (s.ok && s.remaining != 0) s.ok = FALSE;
    fclose(file);
    const char* problem = !s.ok ? "the file is truncated or corrupt" : (type == PBJ_MESH ? PbjMeshProblem(&mesh) : NULL);
    if (problem) {
        logerror("Unable to load PBJ file \"%s\": %s", filepath, problem);
        PbjFreeMesh(&mesh);
        PrismFreeTextureSet(&textures);
        FreeSceneTexture(&texture);
        if (texname) EZ_FREE(texname);
        if (name) EZ_FREE(name);
        return FALSE;
    }
    PrismSubmitTextureSet(&textures);
    PrismRemapMaterialTextures(&textures, &material);
    light.texture = PrismRemapTexture(&textures, light.texture);
    for (size_t i = 0; i < mesh.materials.size; i++) PrismRemapMaterialTextures(&textures, &(mesh.materials.data[i]));
    const char* label = name && name[0] ? name : ez_strip_filename(filepath);
    size_t index = 0;
    switch (type) {
        case PBJ_CAMERA:
            index = SubmitNamedCamera(camera, label);
            break;
        case PBJ_MATERIAL:
            index = SubmitNamedMaterial(material, label);
            break;
        case PBJ_LIGHT:
            index = SubmitNamedLight(light, label);
            break;
        case PBJ_MESH: {
            const char* anim_problem = NULL;
            BOOL has_anim = mesh.anims.size > 0 || mesh.skins.size > 0 || mesh.pose.size > 0;
            if (has_anim) anim_problem = PbjAnimationProblem(&mesh);
            if (anim_problem) logwarn("Loading \"%s\" without its animation: %s", filepath, anim_problem);
            index = PbjSubmitMesh(&mesh, label, has_anim && !anim_problem);
            break;
        }
        case PBJ_FORCE:
            SubmitForce(force, label);
            index = NumForces() - 1;
            break;
        case PBJ_SOURCE:
            source.timer = source.lifetime;
            SubmitSource(source, label);
            index = NumSources() - 1;
            break;
        case PBJ_TEXTURE:
            index = SubmitNamedTexture(texture, label);
            break;
        default: break;
    }
    if (type == PBJ_LIGHT && light.mode == LIGHT_MODE_IBL) UpdateTextures(); // importance table for the environment
    loginfo("Loaded %s \"%s\" from \"%s\"", PbjTypeName(type), label, filepath);
    PrismFreeTextureSet(&textures);
    if (texname) EZ_FREE(texname);
    PbjFreeMesh(&mesh);
    if (name) EZ_FREE(name);
    if (loaded_type) *loaded_type = type;
    if (loaded_index) *loaded_index = index;
    return TRUE;
}

BOOL SavePRISM(const char* filepath) {
    if (!filepath) {
        logerror("Cannot save to a NULL filepath");
        return FALSE;
    }
    FILE* file = fopen(filepath, "wb");
    if (!file) {
        logerror("Unable to open PRISM file \"%s\": %s", filepath, strerror(errno));
        return FALSE;
    }
    PrismStream s = { file, TRUE, 0 };
    uint32_t version = PRISM_VERSION;
    uint64_t geomsize = sizeof(Geometry);
    PrismWrite(&s, PRISM_MAGIC, 4);
    PrismWrite(&s, &version, sizeof(version));
    PrismWrite(&s, &geomsize, sizeof(geomsize));
    PrismWriteGeometry(&s, &(RawRendererRef()->geometry));
    size_t sss_words = 0;
    uint32_t* sss = SubsurfaceSerialize(&sss_words);
    PrismWriteSize(&s, sss_words);
    PrismWrite(&s, sss, sss_words * sizeof(uint32_t));
    EZ_FREE(sss);
    PrismWriteSize(&s, NumTextures());
    for (size_t i = 0; i < NumTextures(); i++) PrismWriteTexture(&s, i);
    if (fclose(file) != 0) s.ok = FALSE;
    if (!s.ok) {
        logerror("Failed while writing PRISM file \"%s\"", filepath);
        return FALSE;
    }
    loginfo("Saved session to \"%s\"", filepath);
    return TRUE;
}

BOOL SavePBJ(PBJType type, size_t index, const char* filepath) {
    if (!filepath) {
        logerror("Cannot save to a NULL filepath");
        return FALSE;
    }
    const char* name = NULL;
    switch (type) {
        case PBJ_CAMERA: if (index < NumCameras()) name = CameraName(index); break;
        case PBJ_MATERIAL: if (index < NumMaterials()) name = MaterialName(index); break;
        case PBJ_LIGHT: if (index < NumLights()) name = LightName(index); break;
        case PBJ_MESH: if (index < NumMeshes()) name = *MeshNameReference(index); break;
        case PBJ_FORCE: if (index < NumForces()) name = *ForceNameReference(index); break;
        case PBJ_SOURCE: if (index < NumSources()) name = *SourceNameReference(index); break;
        case PBJ_TEXTURE: if (index < NumTextures()) name = TextureName(index); break;
        default: break;
    }
    if (!name) {
        logerror("Invalid %s index %zu - unable to save PBJ to \"%s\"", PbjTypeName(type), index, filepath);
        return FALSE;
    }
    FILE* file = fopen(filepath, "wb");
    if (!file) {
        logerror("Unable to open PBJ file \"%s\": %s", filepath, strerror(errno));
        return FALSE;
    }
    PrismStream s = { file, TRUE, 0 };
    uint32_t version = PBJ_VERSION;
    uint32_t tag = (uint32_t)type;
    PrismWrite(&s, PBJ_MAGIC, 3);
    PrismWrite(&s, &version, sizeof(version));
    PrismWrite(&s, &tag, sizeof(tag));
    PrismWriteString(&s, name);
    ARRLIST_uint32_t texhandles = { 0 };
    switch (type) {
        case PBJ_CAMERA: PrismWrite(&s, GetCamera(index), sizeof(SceneCamera)); break;
        case PBJ_MATERIAL: {
            SurfaceMaterial m = *MaterialReference(index);
            PrismCollectMaterialTextures(&texhandles, &m);
            PrismWrite(&s, &m, sizeof(SurfaceMaterial));
            PrismWriteTextureList(&s, texhandles.data, texhandles.size);
            break;
        }
        case PBJ_LIGHT: {
            SceneLight l = *LightReference(index);
            l.texture = PrismCollectTexture(&texhandles, l.texture);
            PrismWrite(&s, &l, sizeof(SceneLight));
            PrismWriteTextureList(&s, texhandles.data, texhandles.size);
            break;
        }
        case PBJ_MESH: PbjWriteMesh(&s, index); break;
        case PBJ_FORCE: PrismWrite(&s, ForceReference(index), sizeof(FluidForce)); break;
        case PBJ_SOURCE: PrismWrite(&s, SourceReference(index), sizeof(FluidSource)); break;
        case PBJ_TEXTURE: PrismWriteTexture(&s, index); break;
        default: break;
    }
    ARRLIST_uint32_t_clear(&texhandles);
    if (fclose(file) != 0) s.ok = FALSE;
    if (!s.ok) {
        logerror("Failed while writing PBJ file \"%s\"", filepath);
        return FALSE;
    }
    loginfo("Saved %s \"%s\" to \"%s\"", PbjTypeName(type), name, filepath);
    return TRUE;
}
