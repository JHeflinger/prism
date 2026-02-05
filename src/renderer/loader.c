#include "loader.h"
#include "renderer/renderer.h"
#include "core/file.h"
#include <raylib.h>
#include <errno.h>
#include <ctype.h>

#define MAX_MTLLIB_PATH_SIZE 1024
#define MAX_OBJ_LINE_SIZE 2048
#define MAX_OBJ_ARG_SIZE 32
#define MAX_OBJ_NUM_ARGS 64
#define MAX_MTL_LINE_SIZE 2048
#define MAX_MTL_ARG_SIZE 32
#define MAX_MTL_NUM_ARGS 64

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
    float x;
    float y;
    float z;
} Vertex;

typedef struct {
    float u;
    float v;
} UV;

typedef struct {
    size_t a;
    size_t b;
    size_t c;
} Face;

DECLARE_ARRLIST(Vertex);
DECLARE_ARRLIST(Face);
DECLARE_ARRLIST(UV);
DECLARE_ARRLIST(UseMaterialMarker);
IMPL_ARRLIST(Vertex);
IMPL_ARRLIST(Face);
IMPL_ARRLIST(UV);
IMPL_ARRLIST(UseMaterialMarker);

typedef struct {
    ARRLIST_Vertex vertices;
    ARRLIST_Vertex normals; // NOTE: per-primitive normals are not implemented yet! This field doesn't have any effect yet!
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
    ARRLIST_Vertex_clear(&(state->vertices));
    ARRLIST_Vertex_clear(&(state->normals));
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

void PruneFaceOBJ(char* str) {
    for (int i = 0; i < MAX_OBJ_ARG_SIZE && str[i] != 0; i++) {
        if (str[i] == '/') {
            str[i] = 0;
            return;
        }
    }
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
    float x, y, z;
    if (!(ParseFloat(lineargs[1], &x) && ParseFloat(lineargs[2], &y) && ParseFloat(lineargs[3], &z))) {
        EZ_ERROR("Invalid vertex fields - expected 3 floats and got \"%s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3]);
        return FALSE;
    }
    ARRLIST_Vertex_add(&(state->vertices), (Vertex){x, y, z});
    return TRUE;
}

BOOL ParseOBJ_vn(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 4) {
        EZ_ERROR("Cannot parse a vertex normal field without exactly 3 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float x, y, z;
    if (!(ParseFloat(lineargs[1], &x) && ParseFloat(lineargs[2], &y) && ParseFloat(lineargs[3], &z))) {
        EZ_ERROR("Invalid vertex normal fields - expected 3 floats and got \"%s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3]);
        return FALSE;
    }
    ARRLIST_Vertex_add(&(state->normals), (Vertex){x, y, z});
    return TRUE;
}

BOOL ParseOBJ_vt(char lineargs[MAX_OBJ_NUM_ARGS][MAX_OBJ_ARG_SIZE], size_t numargs, StateOBJ* state) {
    if (numargs != 3) {
        EZ_ERROR("Cannot parse a vertex texture field without exactly 2 arguments - detected %d instead", (int)numargs - 1);
        return FALSE;
    }
    float u, v;
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
    PruneFaceOBJ(lineargs[1]);
    PruneFaceOBJ(lineargs[2]);
    PruneFaceOBJ(lineargs[3]);
    PruneFaceOBJ(lineargs[4]);
    int64_t indices[4] = { 0 };
    if (!(ParseLInt(lineargs[1], &(indices[0])) &&
          ParseLInt(lineargs[2], &(indices[1])) &&
          ParseLInt(lineargs[3], &(indices[2])) &&
          (numargs == 5 ? ParseLInt(lineargs[4], &(indices[3])) : TRUE))) {
        if (numargs == 5) {
            EZ_ERROR("Invalid face fields - expected 4 unsigned ints and got \"%s %s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3], lineargs[4]);
        } else {
            EZ_ERROR("Invalid face fields - expected 3 unsigned ints and got \"%s %s %s\" instead", lineargs[1], lineargs[2], lineargs[3]);
        }
        return FALSE;
    }
    if (numargs == 4) indices[3] = indices[2];
    for (int i = 0; i < 4; i++) {
        if (indices[i] == 0) {
            EZ_ERROR("Invalid face field detected - there is no vertex 0");
            return FALSE;
        }
        if (indices[i] < 0) {
            indices[i] = state->vertices.size + indices[i];
        } else {
            indices[i]--;
        }
    }
    ARRLIST_Face_add(&(state->faces), (Face){indices[0], indices[1], indices[2]});
    if (numargs == 5) ARRLIST_Face_add(&(state->faces), (Face){indices[0], indices[2], indices[3]});
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
            EZ_ERROR("Face %d references an invalid vertex %d - there are only %d vertices available", (int)i, (int)state.faces.data[i].c, (int)state.vertices.size);
            failure = TRUE;
        }
    }
    if (failure) return FALSE;
    ARRLIST_MaterialID ids = { 0 }; 
    for (size_t i = 0; i < state.materials.size; i++)
        ARRLIST_MaterialID_add(&ids, SubmitMaterial(state.materials.data[i]));
    size_t current_marker = 0;
    MaterialID current_material = 0;
    for (size_t i = 0; i < state.faces.size; i++) {
        while (current_marker < state.markers.size && state.markers.data[current_marker].face_index <= i) {
            current_material = ids.data[state.markers.data[current_marker].material_ind];
            current_marker++;
        }
        Triangle triangle = {
            {
                state.vertices.data[state.faces.data[i].a].x,
                state.vertices.data[state.faces.data[i].a].y,
                state.vertices.data[state.faces.data[i].a].z,
            },
            {
                state.vertices.data[state.faces.data[i].b].x,
                state.vertices.data[state.faces.data[i].b].y,
                state.vertices.data[state.faces.data[i].b].z,
            },
            {
                state.vertices.data[state.faces.data[i].c].x,
                state.vertices.data[state.faces.data[i].c].y,
                state.vertices.data[state.faces.data[i].c].z,
            },
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

BOOL LoadScene(const char* filepath) {
    // TODO:
    return FALSE;
}
