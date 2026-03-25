#include "simulator.h"
#include "renderer/rmath.h"

#define IND(x, y, z) SimIndex(*fsim, x, y, z)

void SetDensityBoundary(float* d, FluidSimulation* fsim) {
    for (size_t k = 1; k <= fsim->length; k++) {
        for (size_t j = 1; j <= fsim->height; j++) {
            d[IND(0,j, k)] = d[IND(1,j,k)];
            d[IND(fsim->width+1,j,k)] = d[IND(fsim->width,j,k)];
        }
    }
    for (size_t k = 1; k <= fsim->length; k++) {
        for (size_t i = 1; i <= fsim->width; i++) {
            d[IND(i, 0,k)] = d[IND(i,1,k)];
            d[IND(i, fsim->height+1, k)] = d[IND(i, fsim->height, k)];
        }
    }
    for (size_t j = 1; j <= fsim->height; j++) {
        for (size_t i = 1; i <= fsim->width; i++) {
            d[IND(i, j, 0)] = d[IND(i, j, 1)];
            d[IND(i, j, fsim->length+1)] = d[IND(i, j, fsim->length)];
        }
    }
    for (size_t i = 1; i <= fsim->width; i++) {
        d[IND(i, 0,0)] = 0.5f * (d[IND(i, 1,0)] + d[IND(i, 0,1)]);
        d[IND(i, fsim->height+1,0)] = 0.5f * (d[IND(i, fsim->height,0)] + d[IND(i, fsim->height+1,1)]);
        d[IND(i, 0,fsim->length+1)] = 0.5f * (d[IND(i, 1,fsim->length+1)] + d[IND(i, 0,fsim->length )]);
        d[IND(i, fsim->height+1,fsim->length+1)] = 0.5f * (d[IND(i, fsim->height, fsim->length+1)] + d[IND(i, fsim->height+1,fsim->length )]);
    }
    for (size_t j = 1; j <= fsim->height; j++) {
        d[IND(0,j, 0)] = 0.5f * (d[IND(1,j, 0)] + d[IND(0,j, 1)]);
        d[IND(fsim->width+1, j, 0)] = 0.5f * (d[IND(fsim->width, j, 0)] + d[IND(fsim->width+1, j, 1)]);
        d[IND(0,j, fsim->length+1)] = 0.5f * (d[IND(1,j, fsim->length+1)] + d[IND(0,j, fsim->length )]);
        d[IND(fsim->width+1, j, fsim->length+1)] = 0.5f * (d[IND(fsim->width, j, fsim->length+1)] + d[IND(fsim->width+1, j, fsim->length )]);
    }
    for (size_t k = 1; k <= fsim->length; k++) {
        d[IND(0,0,k)] = 0.5f * (d[IND(1,0,k)] + d[IND(0,1,k)]);
        d[IND(fsim->width+1, 0,k)] = 0.5f * (d[IND(fsim->width, 0,k)] + d[IND(fsim->width+1, 1,k)]);
        d[IND(0,fsim->height+1, k)] = 0.5f * (d[IND(1,fsim->height+1, k)] + d[IND(0,fsim->height, k)]);
        d[IND(fsim->width+1, fsim->height+1, k)] = 0.5f * (d[IND(fsim->width, fsim->height+1, k)] + d[IND(fsim->width+1, fsim->height, k)]);
    }
    d[IND(0,0,0)] = (1.0f/3.0f) * (d[IND(1,0,0)] + d[IND(0,1,0)] + d[IND(0,0,1)]);
    d[IND(fsim->width+1, 0,0)] = (1.0f/3.0f) * (d[IND(fsim->width, 0,0)] + d[IND(fsim->width+1, 1,0)] + d[IND(fsim->width+1, 0,1)]);
    d[IND(0,fsim->height+1, 0)] = (1.0f/3.0f) * (d[IND(1,fsim->height+1, 0)] + d[IND(0,fsim->height,0)] + d[IND(0,fsim->height+1, 1)]);
    d[IND(fsim->width+1, fsim->height+1, 0)] = (1.0f/3.0f) * (d[IND(fsim->width, fsim->height+1, 0)] + d[IND(fsim->width+1, fsim->height,0)] + d[IND(fsim->width+1, fsim->height+1, 1)]);
    d[IND(0,0,fsim->length+1)] = (1.0f/3.0f) * (d[IND(1,0,fsim->length+1)] + d[IND(0,1,fsim->length+1)] + d[IND(0,0,fsim->length )]);
    d[IND(fsim->width+1, 0,fsim->length+1)] = (1.0f/3.0f) * (d[IND(fsim->width, 0,fsim->length+1)] + d[IND(fsim->width+1, 1,fsim->length+1)] + d[IND(fsim->width+1, 0,fsim->length )]);
    d[IND(0,fsim->height+1, fsim->length+1)] = (1.0f/3.0f) * (d[IND(1,fsim->height+1, fsim->length+1)] + d[IND(0,fsim->height,fsim->length+1)] + d[IND(0,fsim->height+1, fsim->length )]);
    d[IND(fsim->width+1, fsim->height+1, fsim->length+1)] = (1.0f/3.0f) * (d[IND(fsim->width, fsim->height+1, fsim->length+1)] + d[IND(fsim->width+1, fsim->height,fsim->length+1)] + d[IND(fsim->width+1, fsim->height+1, fsim->length )]);
}

void SetVelocityBoundary(vec3 *v, FluidSimulation* fsim) {
    for (size_t k = 1; k <= fsim->length; k++) {
        for (size_t j = 1; j <= fsim->height; j++) {
            v[IND(0, j, k)][0] = -v[IND(1, j, k)][0];
            v[IND(0, j, k)][1] =  v[IND(1, j, k)][1];
            v[IND(0, j, k)][2] =  v[IND(1, j, k)][2];
            v[IND(fsim->width+1, j, k)][0] = -v[IND(fsim->width, j, k)][0];
            v[IND(fsim->width+1, j, k)][1] =  v[IND(fsim->width, j, k)][1];
            v[IND(fsim->width+1, j, k)][2] =  v[IND(fsim->width, j, k)][2];
        }
    }
    for (size_t k = 1; k <= fsim->length; k++) {
        for (size_t i = 1; i <= fsim->width; i++) {
            v[IND(i, 0, k)][0] =  v[IND(i, 1, k)][0];
            v[IND(i, 0, k)][1] = -v[IND(i, 1, k)][1];
            v[IND(i, 0, k)][2] =  v[IND(i, 1, k)][2];
            v[IND(i, fsim->height+1, k)][0] =  v[IND(i, fsim->height, k)][0];
            v[IND(i, fsim->height+1, k)][1] = -v[IND(i, fsim->height, k)][1];
            v[IND(i, fsim->height+1, k)][2] =  v[IND(i, fsim->height, k)][2];
        }
    }
    for (size_t j = 1; j <= fsim->height; j++) {
        for (size_t i = 1; i <= fsim->width; i++) {
            v[IND(i, j, 0)][0] =  v[IND(i, j, 1)][0];
            v[IND(i, j, 0)][1] =  v[IND(i, j, 1)][1];
            v[IND(i, j, 0)][2] = -v[IND(i, j, 1)][2];
            v[IND(i, j, fsim->length+1)][0] =  v[IND(i, j, fsim->length)][0];
            v[IND(i, j, fsim->length+1)][1] =  v[IND(i, j, fsim->length)][1];
            v[IND(i, j, fsim->length+1)][2] = -v[IND(i, j, fsim->length)][2];
        }
    }
    for (size_t i = 1; i <= fsim->width; i++) {
        glm_vec3_add(v[IND(i,1,0)], v[IND(i,0,1)], v[IND(i,0,0)]);
        glm_vec3_scale(v[IND(i,0,0)], 0.5f, v[IND(i,0,0)]);
        glm_vec3_add(v[IND(i,fsim->height,0)], v[IND(i,fsim->height+1,1)], v[IND(i,fsim->height+1,0)]);
        glm_vec3_scale(v[IND(i,fsim->height+1,0)], 0.5f, v[IND(i,fsim->height+1,0)]);
        glm_vec3_add(v[IND(i,1,fsim->length+1)], v[IND(i,0,fsim->length )], v[IND(i,0,fsim->length+1)]);
        glm_vec3_scale(v[IND(i,0,fsim->length+1)], 0.5f, v[IND(i,0,fsim->length+1)]);
        glm_vec3_add(v[IND(i,fsim->height,fsim->length+1)], v[IND(i,fsim->height+1,fsim->length )], v[IND(i,fsim->height+1,fsim->length+1)]);
        glm_vec3_scale(v[IND(i,fsim->height+1,fsim->length+1)], 0.5f, v[IND(i,fsim->height+1,fsim->length+1)]);
    }
    for (size_t j = 1; j <= fsim->height; j++) {
        glm_vec3_add(v[IND(1,j,0)], v[IND(0,j,1)], v[IND(0,j,0)]);
        glm_vec3_scale(v[IND(0,j,0)], 0.5f, v[IND(0,j,0)]);
        glm_vec3_add(v[IND(fsim->width, j,0)], v[IND(fsim->width+1, j,1)], v[IND(fsim->width+1, j,0)]);
        glm_vec3_scale(v[IND(fsim->width+1, j,0)], 0.5f, v[IND(fsim->width+1, j,0)]);
        glm_vec3_add(v[IND(1,j,fsim->length+1)], v[IND(0,j,fsim->length )], v[IND(0,j,fsim->length+1)]);
        glm_vec3_scale(v[IND(0,j,fsim->length+1)], 0.5f, v[IND(0,j,fsim->length+1)]);
        glm_vec3_add(v[IND(fsim->width, j,fsim->length+1)], v[IND(fsim->width+1, j,fsim->length )], v[IND(fsim->width+1, j,fsim->length+1)]);
        glm_vec3_scale(v[IND(fsim->width+1, j,fsim->length+1)], 0.5f, v[IND(fsim->width+1, j,fsim->length+1)]);
    }
    for (size_t k = 1; k <= fsim->length; k++) {
        glm_vec3_add(v[IND(1,0,k)], v[IND(0,1,k)], v[IND(0,0,k)]);
        glm_vec3_scale(v[IND(0,0,k)], 0.5f, v[IND(0,0,k)]);
        glm_vec3_add(v[IND(fsim->width,0,k)], v[IND(fsim->width+1,1,k)], v[IND(fsim->width+1,0,k)]);
        glm_vec3_scale(v[IND(fsim->width+1,0,k)],0.5f, v[IND(fsim->width+1,0,k)]);
        glm_vec3_add(v[IND(1,fsim->height+1,k)], v[IND(0,fsim->height,k)], v[IND(0,fsim->height+1,k)]);
        glm_vec3_scale(v[IND(0,fsim->height+1,k)], 0.5f, v[IND(0,fsim->height+1,k)]);
        glm_vec3_add(v[IND(fsim->width,fsim->height+1,k)], v[IND(fsim->width+1,fsim->height,k)], v[IND(fsim->width+1,fsim->height+1,k)]);
        glm_vec3_scale(v[IND(fsim->width+1,fsim->height+1, k)], 0.5f, v[IND(fsim->width+1,fsim->height+1,k)]);
    }
    vec3 tmp;
    #define CORNER3(ia,ja,ka, ib,jb,kb, ic,jc,kc, id,jd,kd) \
        glm_vec3_add(v[IND(ib,jb,kb)], v[IND(ic,jc,kc)], tmp); \
        glm_vec3_add(tmp, v[IND(id,jd,kd)], tmp); \
        glm_vec3_scale(tmp, 1.f/3.f, v[IND(ia,ja,ka)]);
    CORNER3(0,0,0,1,0,0,0,1,0,0,0,1)
    CORNER3(fsim->width+1,0,0,fsim->width,0,0,fsim->width+1,1,0,fsim->width+1,0,1)
    CORNER3(0,fsim->height+1,0,1,fsim->height+1,0,0,fsim->height,0,0,fsim->height+1,1)
    CORNER3(fsim->width+1,fsim->height+1,0,fsim->width,fsim->height+1,0,fsim->width+1,fsim->height,0,fsim->width+1,fsim->height+1,1)
    CORNER3(0,0,fsim->length+1,1,0,fsim->length+1,0,1,fsim->length+1,0,0,fsim->length )
    CORNER3(fsim->width+1,0,fsim->length+1,fsim->width,0,fsim->length+1,fsim->width+1,1,fsim->length+1,fsim->width+1,0,fsim->length)
    CORNER3(0,fsim->height+1,fsim->length+1,1,fsim->height+1,fsim->length+1,0,fsim->height,fsim->length+1,0,fsim->height+1,fsim->length)
    CORNER3(fsim->width+1,fsim->height+1,fsim->length+1,fsim->width,fsim->height+1,fsim->length+1,fsim->width+1,fsim->height,fsim->length+1,fsim->width+1,fsim->height+1,fsim->length)
    #undef CORNER3
}

void AddForce(vec3* v, FluidForce f, FluidSimulation* fsim) {
    if (f.global) {
        size_t ss = SimSize(*fsim);
        for (size_t i = 0; i < ss; i++) {
            v[i][0] += fsim->timestep * f.force[0];
            v[i][1] += fsim->timestep * f.force[1];
            v[i][2] += fsim->timestep * f.force[2];
        }
    } else {
        size_t minx = MIN(f.x + 1, fsim->width - 1);
        size_t miny = MIN(f.y + 1, fsim->height - 1);
        size_t minz = MIN(f.z + 1, fsim->length - 1);
        size_t maxx = MIN(f.x + f.width + 1, fsim->width - 1);
        size_t maxy = MIN(f.y + f.height + 1, fsim->height - 1);
        size_t maxz = MIN(f.z + f.length + 1, fsim->length - 1);
        for (size_t i = minx; i < maxx; i++) {
            for (size_t j = miny; j < maxy; j++) {
                for (size_t k = minz; k < maxz; k++) {
                    v[IND(i, j, k)][0] += fsim->timestep * f.force[0];
                    v[IND(i, j, k)][1] += fsim->timestep * f.force[1];
                    v[IND(i, j, k)][2] += fsim->timestep * f.force[2];
                }
            }
        }
    }
}

void AddSource(float* d, FluidSource* s, FluidSimulation* fsim) {
    if (s->timer > 0) {
        s->timer -= fsim->timestep;
        size_t minx = MIN(s->x + 1, fsim->width - 1);
        size_t miny = MIN(s->y + 1, fsim->height - 1);
        size_t minz = MIN(s->z + 1, fsim->length - 1);
        size_t maxx = MIN(s->x + s->width + 1, fsim->width - 1);
        size_t maxy = MIN(s->y + s->height + 1, fsim->height - 1);
        size_t maxz = MIN(s->z + s->length + 1, fsim->length - 1);
        for (size_t i = minx; i < maxx; i++) {
            for (size_t j = miny; j < maxy; j++) {
                for (size_t k = minz; k < maxz; k++) {
                    d[IND(i, j, k)] += fsim->timestep * s->density;
                }
            }
        }
    }
}

void TransportVelocity(vec3* dest, vec3* src, FluidSimulation* fsim) {
    float dtx = fsim->timestep * (float)fsim->width;
    float dty = fsim->timestep * (float)fsim->height;
    float dtz = fsim->timestep * (float)fsim->length;
    for (size_t k = 1; k <= fsim->length; k++) {
        for (size_t j = 1; j <= fsim->height; j++) {
            for (size_t i = 1; i <= fsim->width; i++) {
                const float* v = src[IND(i, j, k)];
                float x = CLAMP(i - dtx * v[0], 0.5f, (float)fsim->width + 0.5f);
                float y = CLAMP(j - dty * v[1], 0.5f, (float)fsim->height + 0.5f);
                float z = CLAMP(k - dtz * v[2], 0.5f, (float)fsim->length + 0.5f);
                int i0 = (int)x;
                int j0 = (int)y;
                int k0 = (int)z;
                int i1 = i0 + 1;
                int j1 = j0 + 1;
                int k1 = k0 + 1;
                float s1 = x - i0;
                float s0 = 1.0f - s1;
                float t1 = y - j0;
                float t0 = 1.0f - t1;
                float r1 = z - k0;
                float r0 = 1.0f - r1;
                for (int l = 0; l < 3; l++) {
                    dest[IND(i, j, k)][l] = 
                        r0*(s0*(t0*src[IND(i0,j0,k0)][l] + t1*src[IND(i0,j1,k0)][l]) +
                        s1*(t0*src[IND(i1,j0,k0)][l] + t1*src[IND(i1,j1,k0)][l])) +
                        r1*(s0*(t0*src[IND(i0,j0,k1)][l] + t1*src[IND(i0,j1,k1)][l]) +
                        s1*(t0*src[IND(i1,j0,k1)][l] + t1*src[IND(i1,j1,k1)][l]));
                }
            }
        }
    }
    SetVelocityBoundary(dest, fsim);
}

void DiffuseVelocity(vec3* dest, vec3* src, FluidSimulation* fsim) {
    float hx  = 1.0f / fsim->width;
    float hy  = 1.0f / fsim->height;
    float hz  = 1.0f / fsim->length;
    float ax  = fsim->timestep * fsim->diffusion / (hx * hx);
    float ay  = fsim->timestep * fsim->diffusion / (hy * hy);
    float az  = fsim->timestep * fsim->diffusion / (hz * hz);
    float inv = 1.0f / (1.0f + 2.0f * (ax + ay + az));
    memcpy(dest, src, sizeof(vec3) * SimSize(*fsim));
    for (size_t iter = 0; iter < fsim->iterations; iter++) {
        for (size_t k = 1; k <= fsim->length; k++) {
            for (size_t j = 1; j <= fsim->height; j++) {
                for (size_t i = 1; i <= fsim->width; i++) {
                    size_t idx = IND(i,j,k);
                    for (int l = 0; l < 3; l++) {
                        dest[idx][l] = (
                            src[idx][l]
                            + ax * (dest[IND(i-1,j,k)][l] + dest[IND(i+1,j,k)][l])
                            + ay * (dest[IND(i,j-1,k)][l] + dest[IND(i,j+1,k)][l])
                            + az * (dest[IND(i,j,k-1)][l] + dest[IND(i,j,k+1)][l])
                        ) * inv;
                    }
                }
            }
        }
        SetVelocityBoundary(dest, fsim);
    }
}

void Project(vec3* dest, FluidSimulation* fsim) {
    float hx = 1.0f / fsim->width;
    float hy = 1.0f / fsim->height;
    float hz = 1.0f / fsim->length;
    for (size_t k = 1; k <= fsim->length; k++) {
        for (size_t j = 1; j <= fsim->height; j++) {
            for (size_t i = 1; i <= fsim->width; i++) {
                fsim->divergence[IND(i,j,k)] = -0.5f * (
                    (dest[IND(i+1,j,k)][0] - dest[IND(i-1,j,k)][0]) / hx +
                    (dest[IND(i,j+1,k)][1] - dest[IND(i,j-1,k)][1]) / hy +
                    (dest[IND(i,j,k+1)][2] - dest[IND(i,j,k-1)][2]) / hz
                );
                fsim->pressure[IND(i,j,k)] = 0.0f;
            }
        }
    }
    SetDensityBoundary(fsim->divergence, fsim);
    SetDensityBoundary(fsim->pressure, fsim);
    float ax  = 1.0f / (hx * hx);
    float ay  = 1.0f / (hy * hy);
    float az  = 1.0f / (hz * hz);
    float inv = 1.0f / (2.0f * (ax + ay + az));
    for (size_t iter = 0; iter < fsim->iterations; iter++) {
        for (size_t k = 1; k <= fsim->length; k++) {
            for (size_t j = 1; j <= fsim->height; j++) {
                for (size_t i = 1; i <= fsim->width; i++) {
                    fsim->pressure[IND(i,j,k)] = (
                        fsim->divergence[IND(i,j,k)]
                        + ax * (fsim->pressure[IND(i-1,j,k)] + fsim->pressure[IND(i+1,j,k)])
                        + ay * (fsim->pressure[IND(i,j-1,k)] + fsim->pressure[IND(i,j+1,k)])
                        + az * (fsim->pressure[IND(i,j,k-1)] + fsim->pressure[IND(i,j,k+1)])
                    ) * inv;
                }
            }
        }
        SetDensityBoundary(fsim->pressure, fsim);
    }
    for (size_t k = 1; k <= fsim->length; k++) {
        for (size_t j = 1; j <= fsim->height; j++) {
            for (size_t i = 1; i <= fsim->width; i++) {
                dest[IND(i,j,k)][0] -= 0.5f * (fsim->pressure[IND(i+1,j,k)] - fsim->pressure[IND(i-1,j,k)]) / hx;
                dest[IND(i,j,k)][1] -= 0.5f * (fsim->pressure[IND(i,j+1,k)] - fsim->pressure[IND(i,j-1,k)]) / hy;
                dest[IND(i,j,k)][2] -= 0.5f * (fsim->pressure[IND(i,j,k+1)] - fsim->pressure[IND(i,j,k-1)]) / hz;
            }
        }
    }
    SetVelocityBoundary(dest, fsim);
}

void DiffuseDensity(float* dest, float* src, FluidSimulation* fsim) {
    float hx  = 1.0f / fsim->width;
    float hy  = 1.0f / fsim->height;
    float hz  = 1.0f / fsim->length;
    float ax  = fsim->timestep * fsim->diffusion / (hx * hx);
    float ay  = fsim->timestep * fsim->diffusion / (hy * hy);
    float az  = fsim->timestep * fsim->diffusion / (hz * hz);
    float inv = 1.0f / (1.0f + 2.0f * (ax + ay + az));
    memcpy(dest, src, sizeof(float) * SimSize(*fsim));
    for (size_t iter = 0; iter < fsim->iterations; iter++) {
        for (size_t k = 1; k <= fsim->length; k++) {
            for (size_t j = 1; j <= fsim->height; j++) {
                for (size_t i = 1; i <= fsim->width; i++) {
                    dest[IND(i,j,k)] = (
                        src[IND(i,j,k)]
                        + ax * (dest[IND(i-1,j,k)] + dest[IND(i+1,j,k)])
                        + ay * (dest[IND(i,j-1,k)] + dest[IND(i,j+1,k)])
                        + az * (dest[IND(i,j,k-1)] + dest[IND(i,j,k+1)])
                    ) * inv;
                }
            }
        }
        SetDensityBoundary(dest, fsim);
    }
}

void TransportDensity(float* dest, float* src, FluidSimulation* fsim) {
    float dtx = fsim->timestep * (float)fsim->width;
    float dty = fsim->timestep * (float)fsim->height;
    float dtz = fsim->timestep * (float)fsim->length;
    for (size_t k = 1; k <= fsim->length; k++) {
        for (size_t j = 1; j <= fsim->height; j++) {
            for (size_t i = 1; i <= fsim->width; i++) {
                const float* v = fsim->velocity[IND(i, j, k)];
                float x = CLAMP(i - dtx * v[0], 0.5f, (float)fsim->width + 0.5f);
                float y = CLAMP(j - dty * v[1], 0.5f, (float)fsim->height + 0.5f);
                float z = CLAMP(k - dtz * v[2], 0.5f, (float)fsim->length + 0.5f);
                int i0 = (int)x;
                int j0 = (int)y;
                int k0 = (int)z;
                int i1 = i0 + 1;
                int j1 = j0 + 1;
                int k1 = k0 + 1;
                float s1 = x - i0;
                float s0 = 1.0f - s1;
                float t1 = y - j0;
                float t0 = 1.0f - t1;
                float r1 = z - k0;
                float r0 = 1.0f - r1;
                dest[IND(i, j, k)] = 
                    r0*(s0*(t0*src[IND(i0,j0,k0)] + t1*src[IND(i0,j1,k0)]) +
                    s1*(t0*src[IND(i1,j0,k0)] + t1*src[IND(i1,j1,k0)])) +
                    r1*(s0*(t0*src[IND(i0,j0,k1)] + t1*src[IND(i0,j1,k1)]) +
                    s1*(t0*src[IND(i1,j0,k1)] + t1*src[IND(i1,j1,k1)]));
            }
        }
    }
    SetDensityBoundary(dest, fsim);
}

void StepVelocity(FluidSimulation* fsim) {
    for (size_t i = 0; i < fsim->forces.size; i++)
        AddForce(fsim->velocity, fsim->forces.data[i], fsim);
    SimSwapV(*fsim);
    TransportVelocity(fsim->velocity, fsim->vswap, fsim);
    SimSwapV(*fsim);
    DiffuseVelocity(fsim->velocity, fsim->vswap, fsim);
    Project(fsim->velocity, fsim);
}

void StepDensity(FluidSimulation* fsim) {
    for (size_t i = 0; i < fsim->sources.size; i++)
        AddSource(fsim->density, &(fsim->sources.data[i]), fsim);
    SimSwapD(*fsim);
    DiffuseDensity(fsim->density, fsim->dswap, fsim);
    SimSwapD(*fsim);
    TransportDensity(fsim->density, fsim->dswap, fsim);
    float decay = 1.0f - fsim->dissipation * fsim->timestep;
    if (decay < 0.0f) decay = 0.0f;
    size_t ss = SimSize(*fsim);
    for (size_t i = 0; i < ss; i++) fsim->density[i] *= decay;
}

void SimulateFluidStep(FluidSimulation* fsim) {
    StepVelocity(fsim);
    StepDensity(fsim);
    memset(fsim->vswap, 0, sizeof(vec3)*SimSize(*fsim));
    memset(fsim->dswap, 0, sizeof(float)*SimSize(*fsim));
}
