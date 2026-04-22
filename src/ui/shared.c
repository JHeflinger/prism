#include "shared.h"
#include "renderer/renderer.h"

char* g_lightmodel_labels[] = { "lambertian", "mirror", "dielectric" };
char* g_arapmodel_labels[] = { "rigid", "cubic" };
char* g_sim_visual_labels[] = { "smoke", "fire", "water", "plasma" };
char* g_debugmode_labels[] = { "none", "normals", "bvh", "bounces" };

size_t DropdownSelectSimVisual(void* data, size_t index) {
    if (index == (size_t)-1) {
        return RendererGeometry()->fluid.style;
    } else {
        RendererGeometry()->fluid.style = index;
    }
    return index;
}

size_t DropdownSelectMaterial(void* data, size_t index) {
    Triangle* triref = (Triangle*)data;
    if (index == (size_t)-1) return triref->material;
    triref->material = index;
    if (NumTriangles() != 0) UpdateTriangles();
    return index;
}

size_t DropdownSelectLightModel(void* data, size_t index) {
    SurfaceMaterial* matref = (SurfaceMaterial*)data;
    if (index == (size_t)-1) {
        switch (matref->model) {
            case 2: return 0;
            case 5: return 1;
            case 7: return 2;
            default: return 0;
        }
    } else {
        switch (index) {
            case 0:
                matref->model = 2;
                break;
            case 1:
                matref->model = 5;
                break;
            case 2:
                matref->model = 7;
                break;
            default: break;
        }
        UpdateMaterials();
    }
    return index;
}

size_t DropdownSelectARAPModel(void* data, size_t index) {
    if (index == (size_t)-1) {
        return RenderConfig()->arap.style;
    } else {
        RenderConfig()->arap.style = index;
    }
    return index;
}

size_t DropdownSelectAnimation(void* data, size_t index) {
    MeshAnimation* animation = (MeshAnimation*)data;
    if (index != (size_t)-1) SwitchAnimation(animation, index);
    return animation->current;
}

size_t DropdownSelectDebugMode(void* data, size_t index) {
    if (index != (size_t)-1) RenderConfig()->debug = (DebugConfig)index;
    return RenderConfig()->debug;
}

char** SimVisualLabels() {
    return g_sim_visual_labels;
}

char** LightModelLabels() {
    return g_lightmodel_labels;
}

char** ARAPModelLabels() {
    return g_arapmodel_labels;
}

char** DebugModeLabels() {
    return g_debugmode_labels;
}
