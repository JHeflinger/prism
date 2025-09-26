#include "test.h"
#include "renderer/renderer.h"
#include <easylogger.h>
#include <raylib.h>
#include <easyobjects.h>
#include <time.h>

#define RENDERW 1600
#define RENDERH 900
#define SIMILARITY_THRESHOLD 0.8
#define CYCLES 60
#ifdef __WIN32
#define OPSYS "WINDOWS"
#else
#define OPSYS "LINUX"
#endif

typedef struct {
    const char* scene;
    float fps;
} TestResult;

DECLARE_ARRLIST(TestResult);
IMPL_ARRLIST(TestResult);

size_t g_memcheck = 0;
BOOL g_valid = TRUE;
MaterialID g_generic_mat;
ARRLIST_TestResult g_results = { 0 };

void CreateMaterials() {
	SurfaceMaterial material = {
        { 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f },
        0.2f,
        0,
        0,
        0,
        1.0f,
        0
    };
    g_generic_mat = SubmitMaterial(material);
}

float ImageSimilarity(const char *path1, const char *path2) {
    Image img1 = LoadImage(path1);
    Image img2 = LoadImage(path2);
    if (img1.width != img2.width || img1.height != img2.height) {
        UnloadImage(img1);
        UnloadImage(img2);
        return 0.0f;
    }
    Color *data1 = LoadImageColors(img1);
    Color *data2 = LoadImageColors(img2);
    long long total_diff = 0;
    long long max_diff = (long long)img1.width * img1.height * 3 * 255;
    for (int i = 0; i < img1.width * img1.height; i++) {
        total_diff += abs(data1[i].r - data2[i].r);
        total_diff += abs(data1[i].g - data2[i].g);
        total_diff += abs(data1[i].b - data2[i].b);
    }
    UnloadImage(img1);
    UnloadImage(img2);
    RL_FREE(data1);
    RL_FREE(data2);
    float similarity = 100.0f * (1.0f - (float)total_diff / max_diff);
    if (similarity < 0.0f) similarity = 0.0f;
    return similarity;
}

void CheckSimilarity(const char* path1, const char* path2) {
    printf("Verifying similarity...\n");
    float similarity = ImageSimilarity(path1, path2);
    printf("Image similarity was maintained to be %s%.3f%%%s. ", similarity >= 100 ? EZ_GREEN : (similarity > 99 ? EZ_YELLOW : EZ_RED), similarity, EZ_RESET);
    if (similarity >= 100) {
        printf("No meddling needed!\n\n");
    } else if (similarity > 99) {
        printf("Consider verifying and updating test image.\n\n");
    } else {
        g_valid = FALSE;
        printf("Test results are unable to be validated.\n\n");
    }
}

void ImportModel(const char* path, MaterialID material) {
	Model model = LoadModel(path);
    EZ_ASSERT(model.meshCount != 0, "Failed to load model!");
	Mesh mesh = model.meshes[0];
	for (int i = 0; i < mesh.vertexCount / 3; i++) {
        Triangle triangle = {
            {
                mesh.vertices[(i * 3 + 0) * 3 + 0],
                mesh.vertices[(i * 3 + 0) * 3 + 1],
                mesh.vertices[(i * 3 + 0) * 3 + 2]
            },
            {
                mesh.vertices[(i * 3 + 1) * 3 + 0],
                mesh.vertices[(i * 3 + 1) * 3 + 1],
                mesh.vertices[(i * 3 + 1) * 3 + 2]
            },
            {
                mesh.vertices[(i * 3 + 2) * 3 + 0],
                mesh.vertices[(i * 3 + 2) * 3 + 1],
                mesh.vertices[(i * 3 + 2) * 3 + 2]
            },
            material
        };
        SubmitTriangle(triangle);
    }
    UnloadModel(model);
}

void InitializeTestSuite() {
    g_memcheck = EZ_ALLOCATED();
	OverrideResolution(RENDERW, RENDERH);
	SetViewportSlice(RENDERW, RENDERH);
	SetTraceLogLevel(LOG_NONE);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
	InitWindow(1, 1, "Prism Test Suite");
	InitializeRenderer();
    RenderConfig()->async = FALSE;
    CreateMaterials();
    printf("\nEnvironment configuration:\n\tGPU: %s\n\tResolution: %dx%d\n\tOperating System: %s\n\n", GPUModel(), 1600, 900, OPSYS);
}

void CleanTestSuite() {
    ARRLIST_TestResult_clear(&g_results);
	DestroyRenderer();
	EZ_ASSERT(g_memcheck == EZ_ALLOCATED(), "Memory cleanup revealed a leak of %d bytes", (int)(EZ_ALLOCATED() - g_memcheck));
}

void TestScene(const char* scene_name, const char* reference_image) {
    float total = 0;
    for (size_t i = 0; i < CYCLES; i++) {
        float time = GetTime();
        Render();
        time = GetTime() - time;
        total += time;
    }
    float fps = ((double)CYCLES)/total;
	SaveRender("build/test_out.png");
    printf("Rendered %s %d times over %gs for an average of %s%g FPS%s\n", scene_name, CYCLES, total, EZ_GREEN, fps, EZ_RESET);
    CheckSimilarity("build/test_out.png", reference_image);
    TestResult tr = (TestResult){ scene_name, fps };
    ARRLIST_TestResult_add(&g_results, tr);
}

void CleanScene() {
    ClearLights();
    ClearTriangles();
    SimpleCamera c;
    c.position = (Vector3){ 2.11f, 0.0f, 2.133f };
    c.look = (Vector3){ 0.0f, 0.0f, 0.0f };
    c.up = (Vector3){ 0.0f, 0.0f, 1.0f };
    c.fov = 90.0f;
    MoveCamera(c);
}

void TestVikingRoom() {
    CleanScene();
    printf("Testing Viking room...\n");
	ImportModel("assets/models/room.obj", g_generic_mat);
    SubmitLight((PointLight) {
        {1.5, 0.0, 0.5},
        {0.1, 0.1, 0.1},
        {1.0, 0.0, 1.0},
        {0.0, 1.0, 1.0},
    });
    SubmitLight((PointLight) {
        {0.0, 1.5, 0.5},
        {0.1, 0.1, 0.1},
        {0.0, 1.0, 1.0},
        {1.0, 1.0, 0.0},
    });
    TestScene("Viking Room", "assets/tests/room.png");
}

void TestSphere() {
    CleanScene();
    printf("Testing Sphere...\n");
	ImportModel("assets/models/sphere.obj", g_generic_mat);
    SubmitLight((PointLight) {
        {1.5, 0.0, 0.5},
        {0.1, 0.1, 0.1},
        {1.0, 0.0, 1.0},
        {0.0, 1.0, 1.0},
    });
    SubmitLight((PointLight) {
        {0.0, 1.5, 0.5},
        {0.1, 0.1, 0.1},
        {0.0, 1.0, 1.0},
        {1.0, 1.0, 0.0},
    });
    SimpleCamera c;
    c.position = (Vector3){ 1.499f, 1.087f, 1.278f };
    c.look = (Vector3){ 0.0f, 0.0f, 0.0f };
    c.up = (Vector3){ 0.0f, 0.0f, 1.0f };
    c.fov = 90.0f;
    MoveCamera(c);
    TestScene("Sphere", "assets/tests/sphere.png");
}

void SaveResults() {
    if (!g_valid) {
        printf("Unable to save invalid test results.\n\n");
    } else {
        printf("Would you like to save test results? (y/n)\n>> ");
        char result = '\0';
        while (true) {
            scanf("%c", &result);
            if (result != 'y' && result != 'n') {
                printf("Invalid input detected. Please enter \"y\" or \"n\".\n");
            } else {
                break;
            }
        }
        if (result == 'n') {
            printf("\nDiscarding test results...\n\n");
            return;
        } else {
            FILE* file = fopen("statistics.json", "a");
            if (!file) {
                printf("\nUnable to find corresponding statistics.json file. Discarding test results...\n\n");
                return;
            }
            printf("\nSaving test results...\n");
            time_t now = time(NULL);
            fprintf(file, "{\n");
            fprintf(file, "\t\"time\": \"%ld\",\n", (long)now);
            fprintf(file, "\t\"gpu\": \"%s\",\n", GPUModel());
            fprintf(file, "\t\"width\": %d,\n", RENDERW);
            fprintf(file, "\t\"height\": %d,\n", RENDERH);
            fprintf(file, "\t\"platform\": \"%s\",\n", OPSYS);
            fprintf(file, "\t\"scenes\": [\n");
            for (size_t i = 0; i < g_results.size; i++) {
                fprintf(file, "\t\t{\n");
                fprintf(file, "\t\t\t\"name\": \"%s\",\n", g_results.data[i].scene);
                fprintf(file, "\t\t\t\"fps\": %g\n", g_results.data[i].fps);
                if (i == g_results.size - 1)
                    fprintf(file, "\t\t}\n");
                else
                    fprintf(file, "\t\t},\n");
            }
            fprintf(file, "\t]\n");
            fprintf(file, "},\n");
            printf("Successfully saved test results!\n\n");
            fclose(file);
        }
    }
}

void Test() {
	EZ_INFO("Starting test suite");
	InitializeTestSuite();
	TestVikingRoom();
    TestSphere();
    SaveResults();
	CleanTestSuite();
	EZ_INFO("Successfully finished test suite");
}
