#include "loader.h"
#include "renderer/renderer.h"
#include "renderer/rmath.h"
#include "core/file.h"
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <raylib.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>

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

#define SET_MTL_FLOAT_FIELD(field, a) state->materials.data[state->materials.size - 1].field = a;
#define SET_MTL_UINT_FIELD(field, a) state->materials.data[state->materials.size - 1].field = a;
#define SET_MTL_VEC3_FIELD(field, x, y, z) { \
    state->materials.data[state->materials.size - 1].field[0] = x; \
    state->materials.data[state->materials.size - 1].field[1] = y; \
    state->materials.data[state->materials.size - 1].field[2] = z; \
}

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
    ARRLIST_UV uvs; // NOTE: textures are not implemented yet! This field doesn't have any effect yet!
    ARRLIST_Face faces;
    ARRLIST_DynamicString material_names;
    ARRLIST_SurfaceMaterial materials;
    ARRLIST_UseMaterialMarker markers;
    const char* filepath;
} StateOBJ;

typedef BOOL (*ParseFuncOBJ)(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t, StateOBJ*);
typedef BOOL (*ParseFuncMTL)(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t, StateOBJ*);

typedef struct {
    const char* filepath;
    VertexID vstart;
    ARRLIST_Animation animations;
} StateFBX;

void _aiv32v3(struct aiVector3D ai, vec3 out) { out[0] = ai.x; out[1] = ai.y; out[2] = ai.z; }
void _aiq2v(struct aiQuaternion ai, versor out) { out[0] = ai.x; out[1] = ai.y; out[2] = ai.z; out[3] = ai.w; }
void _aic2v(struct aiColor3D ai, vec3 out) { out[0] = ai.r; out[1] = ai.g; out[2] = ai.b; }
struct aiColor3D _4d23d(struct aiColor4D c) { return (struct aiColor3D){ c.r, c.g, c.b }; }

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

static BOOL ParseFloat(const char* str, float* value) {
    char *end;
    float result;
    errno = 0;
    if (!str || !value) return FALSE;
    result = strtof(str, &end);
    if (end == str || errno == ERANGE) return FALSE;
    while (isspace((unsigned char)*end)) end++;
    if (*end != '\0') return FALSE;
    *value = result;
    return TRUE;
}

static BOOL ParseUInt(const char* str, uint32_t* value) {
    char *end;
    unsigned long result;
    errno = 0;
    if (!str || !value) return FALSE;
    result = strtoul(str, &end, 10);
    if (end == str || errno == ERANGE) return FALSE;
    while (isspace((unsigned char)*end)) end++;
    if (*end != '\0') return FALSE;
    *value = (uint32_t)result;
    return TRUE;
}

static BOOL ParseLInt(const char* str, int64_t* value) {
    char *end;
    long long result;
    errno = 0;
    if (!str || !value) return FALSE;
    result = strtoll(str, &end, 10);
    if (end == str || errno == ERANGE) return FALSE;
    while (isspace((unsigned char)*end)) end++;
    if (*end != '\0') return FALSE;
    *value = (int64_t)result;
    return TRUE;
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
    BOOL success = ParseLInt(abuff, a);
    success &= (*a != 0);
    if (success && strlen(bbuff) > 0) {
        success &= ParseLInt(bbuff, b) & (*b != 0);
    } else {
        b = 0;
    }
    if (success && strlen(cbuff) > 0) {
        success &= ParseLInt(cbuff, c) & (*c != 0);
    } else {
        c = 0;
    }
    return success;
}

static BOOL ParseMTL_illum(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 2) {
        EZ_ERROR("Cannot parse illumination model (illum) without exactly 1 argument - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    uint32_t a;
    if (!(ParseUInt(lineargs[1], &a))) {
        EZ_ERROR("Invalid illumination model (illum) - expected 1 unsigned integer and got \"%s\" instead", lineargs[1]);
        return FALSE;
    }
    if (a > 10) {
        EZ_ERROR("Invalid illumination model (illum) - expected a value from 0 to 10, and got \"%s\" instead", lineargs[1]);
        return FALSE;
    }
    if (a != 2 && a != 5 && a != 7) {
        EZ_WARN("Unhandled illumination model \"%d\" (illum) detected - defaulting to lambertian model instead (2)", a);
        a = 2;
    }
    SET_MTL_UINT_FIELD(model, a);
    return TRUE;
}

static BOOL ParseMTL_Ni(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 2) {
        EZ_ERROR("Cannot parse index of refraction field (Ni) without exactly 1 argument - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float a;
    if (!(ParseFloat(lineargs[1], &a))) {
        EZ_ERROR("Invalid index of refraction field (Ni) - expected 1 float and got \"%s\" instead", lineargs[1]);
        return FALSE;
    }
    SET_MTL_FLOAT_FIELD(ior, a);
    return TRUE;
}

static BOOL ParseMTL_Ns(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 2) {
        EZ_ERROR("Cannot parse specular shininess field (Ns) without exactly 1 argument - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float a;
    if (!(ParseFloat(lineargs[1], &a))) {
        EZ_ERROR("Invalid specular shininess field (Ns) - expected 1 float and got \"%s\" instead", lineargs[1]);
        return FALSE;
    }
    SET_MTL_FLOAT_FIELD(shiny, a);
    return TRUE;
}

static BOOL ParseMTL_Ke(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 4) {
        EZ_ERROR("Cannot parse an emission field (Ke) without exactly 3 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float x, y, z;
    if (!(ParseFloat(lineargs[1], &x) && ParseFloat(lineargs[2], &y) && ParseFloat(lineargs[3], &z))) {
        EZ_ERROR("Invalid emission (Ke) fields - expected 3 floats and got \"%s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3]);
        return FALSE;
    }
    SET_MTL_VEC3_FIELD(emission, x, y, z);
    return TRUE;
}

static BOOL ParseMTL_Ka(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 4) {
        EZ_ERROR("Cannot parse an ambience field (Ka) without exactly 3 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float x, y, z;
    if (!(ParseFloat(lineargs[1], &x) && ParseFloat(lineargs[2], &y) && ParseFloat(lineargs[3], &z))) {
        EZ_ERROR("Invalid ambience (Ka) fields - expected 3 floats and got \"%s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3]);
        return FALSE;
    }
    SET_MTL_VEC3_FIELD(ambient, x, y, z);
    return TRUE;
}

static BOOL ParseMTL_Kd(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 4) {
        EZ_ERROR("Cannot parse a diffuse field (Kd) without exactly 3 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float x, y, z;
    if (!(ParseFloat(lineargs[1], &x) && ParseFloat(lineargs[2], &y) && ParseFloat(lineargs[3], &z))) {
        EZ_ERROR("Invalid diffuse (Kd) fields - expected 3 floats and got \"%s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3]);
        return FALSE;
    }
    SET_MTL_VEC3_FIELD(diffuse, x, y, z);
    return TRUE;
}

static BOOL ParseMTL_Ks(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 4) {
        EZ_ERROR("Cannot parse a specular field (Ks) without exactly 3 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float x, y, z;
    if (!(ParseFloat(lineargs[1], &x) && ParseFloat(lineargs[2], &y) && ParseFloat(lineargs[3], &z))) {
        EZ_ERROR("Invalid specular (Ks) fields - expected 3 floats and got \"%s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3]);
        return FALSE;
    }
    SET_MTL_VEC3_FIELD(specular, x, y, z);
    return TRUE;
}

static BOOL ParseMTL_Tf(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 4) {
        EZ_ERROR("Cannot parse an absorbtion field (Tf) without exactly 3 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float x, y, z;
    if (!(ParseFloat(lineargs[1], &x) && ParseFloat(lineargs[2], &y) && ParseFloat(lineargs[3], &z))) {
        EZ_ERROR("Invalid absorbtion (Tf) fields - expected 3 floats and got \"%s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3]);
        return FALSE;
    }
    SET_MTL_VEC3_FIELD(absorbtion, x, y, z);
    return TRUE;
}

static BOOL ParseMTL_Rd(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 4) {
        EZ_ERROR("Cannot parse a dispersion field (Rd) without exactly 3 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float x, y, z;
    if (!(ParseFloat(lineargs[1], &x) && ParseFloat(lineargs[2], &y) && ParseFloat(lineargs[3], &z))) {
        EZ_ERROR("Invalid dispersion (Rd) fields - expected 3 floats and got \"%s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3]);
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
        EZ_ERROR("Invalid format for newmtl arguments, must have only 1 argument - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    char* name = EZ_ALLOC(strlen(lineargs[1]), sizeof(char));
    strcpy(name, lineargs[1]);
    ARRLIST_DynamicString_add(&(state->material_names), name);
    ARRLIST_SurfaceMaterial_add(&(state->materials), (SurfaceMaterial){ 0 });
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
        EZ_ERROR("Invalid format for mtllib arguments, must have only 1 argument - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    char mtlloc[MAX_MTLLIB_PATH_SIZE] = { 0 };
    char mtlpath[MAX_MTLLIB_PATH_SIZE] = { 0 };
    strcpy(mtlloc, state->filepath);
    char* fnstart = (char*)StripFilename(mtlloc);
    if (fnstart) fnstart[0] = 0;
    else mtlloc[0] = 0;
    sprintf(mtlpath, "%s%s", mtlloc, lineargs[1]);
    SimpleFile* file = ReadSimpleFile(mtlpath);
    if (!file) {
        EZ_ERROR("Unable to load invalid filepath to mtllib \"%s\"", mtlpath);
        return FALSE;
    }
    if (file->type != DOTMTL) {
        EZ_ERROR("\"%s\" is not a .mtl file. Unable to open it with the MTL loader", mtlpath);
        FreeFile(file);
        return FALSE;
    }
    LineParser parser = Parser(file);
    char line[MAX_MTL_LINE_SIZE] = { 0 };
    BOOL failure = FALSE;
    while (NextLine(&parser, line, MAX_MTL_LINE_SIZE)) {
        char lineargs[MAX_MTL_NUM_ARGS][MAX_MTL_ARG_SIZE] = { 0 };
        size_t numargs = ParseLineArgsOBJ(line, lineargs);
        if (numargs > 0 && lineargs[0][0] != '#') {
            ParseFuncMTL p = GetParserFromArgMTL(lineargs[0]);
            if (p) { if ((failure = !p(lineargs, numargs, state))) { EZ_ERROR("%s:%d - Unable to parse this MTL field due to an error", mtlpath, (int)parser.line); break; }
            } else EZ_WARN("%s:%d - Unknown MTL property detected: \"%s\", skipping parsing this field...", mtlpath, (int)parser.line, lineargs[0]);
        }
    }
    FreeFile(file);
    return !failure;
}

static BOOL ParseOBJ_usemtl(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 2) {
        EZ_ERROR("Invalid format for usemtl arguments, must have only 1 argument - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    size_t ind = 0;
    for (size_t i = 0; i < state->material_names.size; i++) {
        if (strcmp(state->material_names.data[i], lineargs[1]) == 0) {
            ind = i;
            goto found;
        }
    }
    EZ_ERROR("No material of name \"%s\" found", lineargs[1]);
    return FALSE;
    found:
    ARRLIST_UseMaterialMarker_add(&(state->markers), (UseMaterialMarker){ state->faces.size, ind });
    return TRUE;
}

static BOOL ParseOBJ_v(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 4) {
        EZ_ERROR("Cannot parse a vertex field without exactly 3 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    vec3 v;
    if (!(ParseFloat(lineargs[1], &(v[0])) && ParseFloat(lineargs[2], &(v[1])) && ParseFloat(lineargs[3], &(v[2])))) {
        EZ_ERROR("Invalid vertex fields - expected 3 floats and got \"%s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3]);
        return FALSE;
    }
    ARRLIST_vec3_add(&(state->vertices), v);
    return TRUE;
}

static BOOL ParseOBJ_vn(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 4) {
        EZ_ERROR("Cannot parse a vertex normal field without exactly 3 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    vec3 v;
    if (!(ParseFloat(lineargs[1], &(v[0])) && ParseFloat(lineargs[2], &(v[1])) && ParseFloat(lineargs[3], &(v[2])))) {
        EZ_ERROR("Invalid vertex normal fields - expected 3 floats and got \"%s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3]);
        return FALSE;
    }
    ARRLIST_vec3_add(&(state->normals), v);
    return TRUE;
}

static BOOL ParseOBJ_vt(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 3 && numargs != 4) {
        EZ_ERROR("Cannot parse a vertex texture field without 2-3 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float u, v; // ignoring w
    if (!(ParseFloat(lineargs[1], &u) && ParseFloat(lineargs[2], &v))) {
        EZ_ERROR("Invalid vertex texture fields - expected 2 floats and got \"%s %s\" instead", lineargs[1], lineargs[2]);
        return FALSE;
    }
    ARRLIST_UV_add(&(state->uvs), (UV){u, v});
    return TRUE;
}

static BOOL ParseOBJ_f(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 4 && numargs != 5) {
        EZ_ERROR("Cannot parse a face field without exactly 3 or 4 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    int64_t values[4][3] = { 0 };
    int count = -1;
    for (int i = 0; i < (numargs == 4 ? 3 : 4); i++) {
        size_t c;
        if (!ParseTriplet(lineargs[i + 1], &values[i][0], &values[i][1], &values[i][2], &c)) {
            EZ_ERROR("Unable to parse face field \"%s\" into valid indices", lineargs[i + 1]);
            return FALSE;
        }
        if (count < 0) {
            count = (int)c;
        } else if (count != (int)c) {
            EZ_ERROR("Detected a mismatched count in face field indices - expected %d but got %d instead", count, (int)c);
            return FALSE;
        }
    }
    for (int i = 0; i < 2; i++) {
        int zeros = 0;
        for (int j = 0; j < (numargs == 4 ? 3 : 4); j++) {
            if (values[j][i] == 0) zeros++;
        }
        if (zeros != 0 && zeros != (numargs == 4 ? 3 : 4)) {
            EZ_ERROR("Detected a mismatched format count in face field indices - vertex normals/textures should either be all defined or not at all");
            return FALSE;
        }
    }
    for (int i = 0; i < (numargs == 4 ? 3 : 4); i++) {
        if (values[i][0] == 0) {
            EZ_ERROR("Invalid face field detected - there is no vertex 0");
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
            EZ_ERROR("Face %d references an invalid vertex %d - there are only %d vertices available", (int)i, (int)state.faces.data[i].a, (int)state.vertices.size);
            failure = TRUE;
        }
        if (state.faces.data[i].b >= state.vertices.size) {
            EZ_ERROR("Face %d references an invalid vertex %d - there are only %d vertices available", (int)i, (int)state.faces.data[i].b, (int)state.vertices.size);
            failure = TRUE;
        }
        if (state.faces.data[i].c >= state.vertices.size) {
            EZ_ERROR("Face %d references an invalid vertex normal %d - there are only %d vertices available", (int)i, (int)state.faces.data[i].c, (int)state.vertices.size);
            failure = TRUE;
        }
        if (state.faces.data[i].normals && state.faces.data[i].an >= state.normals.size) {
            EZ_ERROR("Face %d references an invalid vertex normal %d - there are only %d normals available", (int)i, (int)state.faces.data[i].an, (int)state.normals.size);
            failure = TRUE;
        }
        if (state.faces.data[i].normals && state.faces.data[i].bn >= state.normals.size) {
            EZ_ERROR("Face %d references an invalid vertex normal %d - there are only %d normals available", (int)i, (int)state.faces.data[i].bn, (int)state.normals.size);
            failure = TRUE;
        }
        if (state.faces.data[i].normals && state.faces.data[i].cn >= state.normals.size) {
            EZ_ERROR("Face %d references an invalid vertex normal %d - there are only %d normals available", (int)i, (int)state.faces.data[i].cn, (int)state.normals.size);
            failure = TRUE;
        }
    }
    if (failure) return FALSE;
    ARRLIST_MaterialID ids = { 0 };
    size_t vertices_start = NumVertices();
    size_t normals_start = NumNormals();
    for (size_t i = 0; i < state.materials.size; i++)
        ARRLIST_MaterialID_add(&ids, SubmitNamedMaterial(state.materials.data[i], state.material_names.data[i]));
    for (size_t i = 0; i < state.vertices.size; i++)
        SubmitVertex(state.vertices.data[i]);
    for (size_t i = 0; i < state.normals.size; i++)
        SubmitNormal(state.normals.data[i]);
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
            current_material
        };
        SubmitTriangle(triangle);
    }
    ARRLIST_MaterialID_clear(&ids);
    return TRUE;
}

BOOL LoadOBJ(const char* filepath) {
    SimpleFile* file = ReadSimpleFile(filepath);
    VertexID startv = NumVertices();
    TriangleID startt = NumTriangles();
    if (!file) {
        EZ_ERROR("Unable to load invalid filepath \"%s\"", filepath);
        return FALSE;
    }
    if (file->type != DOTOBJ) {
        EZ_ERROR("\"%s\" is not a .obj file. Unable to open it with the OBJ loader", filepath);
        FreeFile(file);
        return FALSE;
    }
    LineParser parser = Parser(file);
    StateOBJ state = { 0 };
    state.filepath = filepath;
    char line[MAX_OBJ_LINE_SIZE] = { 0 };
    while (NextLine(&parser, line, MAX_OBJ_LINE_SIZE)) {
        char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE] = { 0 };
        size_t numargs = ParseLineArgsOBJ(line, lineargs);
        if (numargs > 0 && lineargs[0][0] != '#') {
            ParseFuncOBJ p = GetParserFromArgOBJ(lineargs[0]);
            if (p) { if (!p(lineargs, numargs, &state)) { EZ_ERROR("%s:%d - Unable to parse this OBJ field due to an error", filepath, (int)parser.line); CleanStateOBJ(&state); FreeFile(file); return FALSE; }
            } else EZ_WARN("%s:%d - Unknown OBJ property detected: \"%s\", skipping parsing this field...", filepath, (int)parser.line, lineargs[0]);
        }
    }
    if (!ConstructOBJ(state)) {
        EZ_ERROR("Unable to construct .obj \"%s\" due to an error", filepath);
        FreeFile(file);
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
            FALSE, startv, NumVertices() - 1, startt, NumTriangles() - 1, 0, (uint32_t)-1, INLINEV3(center), INLINEV3(extent), { 0 }, { 0 },
            { 1.0f, 1.0f, 1.0f }, GLM_MAT4_IDENTITY_INIT }, StripFilename(filepath));
    }
    CleanStateOBJ(&state);
    FreeFile(file);
    return TRUE;
}

static MaterialID LoadFBXMaterial(const struct aiMaterial* mat) {
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
        EZ_WARN("Skeleton exceeds MAX_BONES (%d) — bone \"%s\" skipped", MAX_BONES, name);
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

static BOOL LoadFBXMesh(Skeleton* skeleton, const struct aiMesh* mesh, MaterialID mat_id, size_t vertices_start, size_t normals_start) {
    if (!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)) {
        EZ_WARN("Non-triangle FBX mesh detected - skipping \"%s\"", mesh->mName.data);
        return TRUE;
    }
    BOOL has_normals = mesh->mNormals != NULL;
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
    for (size_t i = 0; i < mesh->mNumFaces; i++) {
        const struct aiFace* face = &mesh->mFaces[i];
        if (face->mNumIndices != 3) {
            EZ_WARN("FBX face is a non-triangle - skipping");
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
            mat_id
        };
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
        if (!LoadFBXMesh(skeleton, mesh, mat_id, vstart, nstart))
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
                       | aiProcess_FlipUVs              // match your UV convention
                       | aiProcess_PopulateArmatureData; // reconstruct skeleton hierarchy, converting world-space animation channels to local-space
    const struct aiScene* scene = aiImportFile(filepath, flags);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        EZ_ERROR("Failed to load FBX file \"%s\": %s", filepath, aiGetErrorString());
        return FALSE;
    }
    uint32_t starts = NumSkins();
    VertexID startv = NumVertices();
    TriangleID startt = NumTriangles();
    MaterialID* mat_ids = EZ_ALLOC(scene->mNumMaterials, sizeof(MaterialID));
    for (unsigned int i = 0; i < scene->mNumMaterials; i++) mat_ids[i] = LoadFBXMaterial(scene->mMaterials[i]);
    vec3 aabb_min = GLM_VEC3_ZERO_INIT;
    vec3 aabb_max = GLM_VEC3_ZERO_INIT;
    BOOL aabb_init = FALSE;
    Skeleton skeleton = { 0 };
    if (!TraverseFBXNode(&skeleton, scene->mRootNode, scene, mat_ids, &aabb_min, &aabb_max, &aabb_init)) {
        EZ_ERROR("Failed to traverse scene graph for \"%s\"", filepath);
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
            INLINEV3(center), INLINEV3(extent),
            { 0 }, { 0 },
            { 1.0f, 1.0f, 1.0f }, GLM_MAT4_IDENTITY_INIT
        }, StripFilename(filepath));
        size_t num_anims = 0;
        Animation* anims = LoadFBXAnimations(scene, &num_anims);
        for (size_t i = 0; i < num_anims; i++) SubmitAnimation(NumMeshes() - 1, skeleton, anims[i]);
        EZ_FREE(anims);
        if (num_anims > 0) MeshReference(NumMeshes() - 1)->pose = (NumAnimations() - 1) * MAX_BONES;
    }
    aiReleaseImport(scene);
    return TRUE;
}
