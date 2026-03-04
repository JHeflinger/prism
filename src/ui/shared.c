#include "shared.h"
#include "renderer/renderer.h"

char* g_labels[] = { "lambertian", "mirror", "dielectric" };

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

char** LightModelLabels() {
    return g_labels;
}
