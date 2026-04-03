#include "simulate.h"
#include "renderer/renderer.h"
#include "ui/shared.h"

size_t g_simulate_steps = 0;
BOOL g_simulation_running = FALSE;
BOOL g_simulation_started = FALSE;

void DrawSimulatePanel(float width, float height) {
    static size_t s_simwidth = 1;
    static size_t s_simheight = 1;
    static size_t s_simlength = 1;
    static float s_simstep = 0.016f;
    static BOOL s_dynamicts = FALSE;
    static size_t s_simiterations = 20;
    static size_t s_simstepsize = 1;
    FluidSimulation* sim = &(RendererGeometry()->fluid);
    float component_width = (width - 20 - (3 * 15) - (2 * 10)) / 3.0f;
    SetPipelineFlags(SIMULATE_PIPELINE_FLAGS);
    UIDrawText("Simulation Controls");
    UIDivider(width - 20);
    if (sim->width == 0 || sim->height == 0 || sim->length == 0) DisableUI();
    if (UIButton(g_simulation_started ? (g_simulation_running ? "Pause" : "Resume") : "Start", width - 20)) {
        if (g_simulation_running) g_simulation_running = FALSE;
        else g_simulation_running = TRUE;
        g_simulation_started = TRUE;
    }
    if (UIButton("Stop", width - 20)) {
        g_simulate_steps = (size_t)-1;
        g_simulation_running = FALSE;
        g_simulation_started = FALSE;
        RestartSimulation();
    }
    if (UIButton("Step", (width - 20.0f)/2.0f)) {
        g_simulation_running = TRUE;
        g_simulate_steps = s_simstepsize;
    }
    UIMoveCursor((width - 20.0f)/2.0f, -20);
    UIDragSize(&s_simstepsize, 1, 10000, 1, (width - 20.0f)/2.0f);
    UIMoveCursor(0, 35);
    EnableUI();
    UIDrawText("Reconfigure Simulation");
    UIDivider(width - 20);
    UIDrawText("w");
    UIMoveCursor(15, -20);
    UIDragSize(&(s_simwidth), 1, 1000, 1, component_width);
    UIMoveCursor(component_width + 25, -20);
    UIDrawText("h");
    UIMoveCursor(component_width + 40, -20);
    UIDragSize(&(s_simheight), 1, 1000, 1, component_width);
    UIMoveCursor((2*component_width) + 50, -20);
    UIDrawText("l");
    UIMoveCursor((2*component_width) + 65, -20);
    UIDragSize(&(s_simlength), 1, 1000, 1, component_width);
    UIMoveCursor(0, 5);
    UIDrawText("Dynamic Timestep");
    UIMoveCursor((width - 20.0f)/2.0f, -20);
    UICheckbox(&s_dynamicts);
    UIMoveCursor(0, 5);
    if (s_dynamicts) DisableUI();
    UIDrawText("Timestep");
    UIMoveCursor((width - 20.0f)/2.0f, -20);
    UIDragFloat(&s_simstep, 0.0f, FLT_MAX, 0.001f, (width - 20.0f)/2.0f);
    EnableUI();
    UIMoveCursor(0, 5);
    UIDrawText("Solver Iterations");
    UIMoveCursor((width - 20.0f)/2.0f, -20);
    UIDragSize(&s_simiterations, 1, 100, 1, (width - 20.0f)/2.0f);
    UIMoveCursor(0, 10);
    if (UIButton("Reconfigure Simulation", width - 20.0f)) {
        ConfigureSimulation(s_simwidth, s_simheight, s_simlength, s_dynamicts ? 0.0f : s_simstep);
    }
    UIMoveCursor(0, 35);
    UIDrawText("Fluid Properties");
    UIDivider(width - 20);
    UIDrawText("Diffusion");
    UIMoveCursor((width - 20.0f)/2.0f, -20);
    float diffusion = sim->diffusion * 10000.0f;
    UIDragFloat(&(diffusion), 0.0f, FLT_MAX, 0.001f, (width - 20.0f)/2.0f);
    sim->diffusion = diffusion / 10000.0f;
    UIMoveCursor(0, 5);
    UIDrawText("Viscosity");
    UIMoveCursor((width - 20.0f)/2.0f, -20);
    float viscosity = sim->viscosity* 10000.0f;
    UIDragFloat(&(viscosity), 0.0f, FLT_MAX, 0.001f, (width - 20.0f)/2.0f);
    sim->viscosity = viscosity / 10000.0f;
    UIMoveCursor(0, 5);
    UIDrawText("Dissipation");
    UIMoveCursor((width - 20.0f)/2.0f, -20);
    UIDragFloat(&(sim->dissipation), 0.0f, FLT_MAX, 0.001f, (width - 20.0f)/2.0f);
    UIMoveCursor(0, 5);
    UIDrawText("Visualization");
    UIMoveCursor((width - 20.0f)/2.0f, -20);
    UIDropdownMenu((width - 20.0f)/2.0f, 4, SimVisualLabels(), DropdownSelectSimVisual, NULL);
    if (g_simulation_running) {
        StepSimulation();
        if (g_simulate_steps != (size_t)-1) {
            g_simulate_steps--;
            if (g_simulate_steps == 0) {
                g_simulation_running = FALSE;
                g_simulate_steps = (size_t)-1;
            }
        }
    }
}

Panel GenerateSimulatePanel() {
	Panel p = { 0 };
	SetupPanel(&p, "Simulate");
	p.draw = DrawSimulatePanel;
	return p;
}
