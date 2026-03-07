#include "loader.h"
#include "renderer/renderer.h"
#include "renderer/rmath.h"
#include "core/file.h"
#include <raylib.h>
#include <errno.h>
#include <ctype.h>

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

void CleanStateOBJ(StateOBJ* state) {
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

BOOL IsWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

size_t ParseLineArgsOBJ(const char line[MAX_OBJ_LINE_SIZE], char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE]) {
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

BOOL ParseFloat(const char* str, float* value) {
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

BOOL ParseUInt(const char* str, uint32_t* value) {
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

BOOL ParseLInt(const char* str, int64_t* value) {
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

BOOL ParseTriplet(const char* str, int64_t* a, int64_t* b, int64_t* c, size_t* count) {
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

BOOL ParseMTL_illum(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
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
    SET_MTL_UINT_FIELD(model, a);
    return TRUE;
}

BOOL ParseMTL_Ni(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
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

BOOL ParseMTL_Ns(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
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

BOOL ParseMTL_Ke(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
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

BOOL ParseMTL_Ka(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
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

BOOL ParseMTL_Kd(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
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

BOOL ParseMTL_Ks(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
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

BOOL ParseMTL_Tf(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
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

BOOL ParseMTL_Rd(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
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

BOOL ParseMTL_newmtl(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
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

ParseFuncMTL GetParserFromArgMTL(const char* header) {
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
    #undef GETPARSER
    return NULL;
}

BOOL ParseOBJ_mtllib(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 2) {
        EZ_ERROR("Invalid format for mtllib arguments, must have only 1 argument - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    char mtlloc[MAX_MTLLIB_PATH_SIZE] = { 0 };
    char mtlpath[MAX_MTLLIB_PATH_SIZE] = { 0 };
    strcpy(mtlloc, state->filepath);
    char* fnstart = StripFilename(mtlloc);
    if (fnstart) fnstart[0] = 0;
    else mtlloc[0] = 0;
    sprintf(mtlpath, "%s%s", mtlloc, lineargs[1]);
    SimpleFile* file = ReadFile(mtlpath);
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

BOOL ParseOBJ_usemtl(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
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

BOOL ParseOBJ_v(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
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

BOOL ParseOBJ_vn(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
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

BOOL ParseOBJ_vt(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
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

BOOL ParseOBJ_f(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
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

BOOL ParseOBJ_g(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    // Not useful for prism right now, implement later if needed
    return TRUE;
}

BOOL ParseOBJ_o(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    // Not useful for prism right now, implement later if needed
    return TRUE;
}

BOOL ParseOBJ_s(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    // Not useful for prism right now, implement later if needed
    return TRUE;
}

ParseFuncOBJ GetParserFromArgOBJ(const char* header) {
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

BOOL ConstructOBJ(const StateOBJ state) {
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
    SimpleFile* file = ReadFile(filepath);
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
    if (!ConstructOBJ(state)) EZ_ERROR("Unable to construct .obj \"%s\" due to an error", filepath);
    CleanStateOBJ(&state);
    FreeFile(file);
    return TRUE;
}

BOOL LoadXML(const char* filepath) { // TODO: marked for removal
    EZ_WARN("The XML loader is experimental/unstable and is marked for removal. It is recommended to load scenes from different formats.");
    SimpleFile* file = ReadFile(filepath);
    if (!file) {
        EZ_ERROR("Unable to load invalid filepath \"%s\"", filepath);
        return FALSE;
    }
    if (file->type != DOTXML) {
        EZ_ERROR("\"%s\" is not a .xml file. Unable to open it with the XML loader", filepath);
        FreeFile(file);
        return FALSE;
    }
    LineParser parser = Parser(file);
    char line[MAX_XML_LINE_SIZE] = { 0 };
    char obj[MAX_XML_LINE_SIZE] = { 0 };
    int camera_params_collected = 0;
    vec3 pos;
    vec3 up;
    vec3 look;
    float heightangle;
    while (NextLine(&parser, line, MAX_XML_LINE_SIZE)) {
        char lineargs[MAX_XML_NUM_ARGS][MAX_XML_ARG_SIZE] = { 0 };
        size_t numargs = ParseLineArgsOBJ(line, lineargs);
        if (numargs > 0) {
            if (strcmp(lineargs[0], "<object") == 0 && numargs == 4) {
                size_t len = strlen(lineargs[3]);
                lineargs[3][len - 2] = 0;
                strcpy(obj, lineargs[3] + 10);
            } else if (strcmp(lineargs[0], "<pos") == 0) {
                camera_params_collected |= 1;
                char x[MAX_XML_ARG_SIZE] = { 0 };
                char y[MAX_XML_ARG_SIZE] = { 0 };
                char z[MAX_XML_ARG_SIZE] = { 0 };
                float xf, yf, zf;
                memcpy(x, lineargs[1] + 3, strlen(lineargs[1]) - 4);
                memcpy(y, lineargs[2] + 3, strlen(lineargs[2]) - 4);
                memcpy(z, lineargs[3] + 3, strlen(lineargs[3]) - 6);
                if (ParseFloat(x, &xf) && ParseFloat(y, &yf) && ParseFloat(z, &zf)) {
                    SETVEC3(pos, xf, yf, zf);
                } else {
                    EZ_ERROR("Unable to parse camera position arguments \"%s %s %s\"", x, y, z);
                }
            } else if (strcmp(lineargs[0], "<up") == 0) {
                camera_params_collected |= 1 << 1;
                char x[MAX_XML_ARG_SIZE] = { 0 };
                char y[MAX_XML_ARG_SIZE] = { 0 };
                char z[MAX_XML_ARG_SIZE] = { 0 };
                float xf, yf, zf;
                memcpy(x, lineargs[1] + 3, strlen(lineargs[1]) - 4);
                memcpy(y, lineargs[2] + 3, strlen(lineargs[2]) - 4);
                memcpy(z, lineargs[3] + 3, strlen(lineargs[3]) - 6);
                if (ParseFloat(x, &xf) && ParseFloat(y, &yf) && ParseFloat(z, &zf)) {
                    SETVEC3(up, xf, yf, zf);
                } else {
                    EZ_ERROR("Unable to parse camera up arguments \"%s %s %s\"", x, y, z);
                }
            } else if (strcmp(lineargs[0], "<focus") == 0) {
                camera_params_collected |= 1 << 2;
                char x[MAX_XML_ARG_SIZE] = { 0 };
                char y[MAX_XML_ARG_SIZE] = { 0 };
                char z[MAX_XML_ARG_SIZE] = { 0 };
                float xf, yf, zf;
                memcpy(x, lineargs[1] + 3, strlen(lineargs[1]) - 4);
                memcpy(y, lineargs[2] + 3, strlen(lineargs[2]) - 4);
                memcpy(z, lineargs[3] + 3, strlen(lineargs[3]) - 6);
                if (ParseFloat(x, &xf) && ParseFloat(y, &yf) && ParseFloat(z, &zf)) {
                    SETVEC3(look, xf, yf, zf);
                } else {
                    EZ_ERROR("Unable to parse camera focus arguments \"%s %s %s\"", x, y, z);
                }
            } else if (strcmp(lineargs[0], "<heightangle") == 0) {
                camera_params_collected |= 1 << 3;
                char x[MAX_XML_ARG_SIZE] = { 0 };
                float xf;
                memcpy(x, lineargs[1] + 3, strlen(lineargs[1]) - 6);
                if (ParseFloat(x, &xf)) {
                    heightangle = xf;
                } else {
                    EZ_ERROR("Unable to parse camera heightangle argument \"%s\"", x);
                }
            }
        }
    }
    if (camera_params_collected == 0) {
        EZ_WARN("No camera parameters collected - default camera settings will be used");
    } else if (camera_params_collected != 0xf) {
        EZ_WARN("Unable to find all required camera parameters - default camera settings will be used");
    } else {
        SimpleCamera camera = GetCamera();
        SETVEC(camera.position, pos);
        SETVEC(camera.look, look);
        SETVEC(camera.up, up);
        camera.fov = heightangle;
        MoveCamera(camera);
        ReorientCamera();
    }
    FreeFile(file);
    if (obj[0] == 0) {
        EZ_ERROR("No mesh found in XML scene to load");
        return FALSE;
    }
    char xmlloc[MAX_XML_PATH_SIZE] = { 0 };
    char objpath[MAX_OBJ_PATH_SIZE*3] = { 0 };
    strcpy(xmlloc, filepath);
    char* fnstart = StripFilename(xmlloc);
    if (fnstart) fnstart[0] = 0;
    else xmlloc[0] = 0;
    sprintf(objpath, "%s%s", xmlloc, obj);
    return LoadOBJ(objpath);
}

BOOL LoadScene(const char* filepath) {
    // TODO:
    EZ_ERROR("LoadScene has not been implemented yet!");
    return FALSE;
}
