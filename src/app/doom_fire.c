#include "doom_fire.h"

#include <math.h>
#include <string.h>

#define PRESSURE_ITERS 14

static inline int iclamp(int x, int lo, int hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline float fclamp(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline float fmin2(float a, float b) { return a < b ? a : b; }
static inline float fmax2(float a, float b) { return a > b ? a : b; }

static inline uint32_t xs32(uint32_t* s) {
    uint32_t x = (*s == 0) ? 1u : *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static inline float rng_f01(uint32_t* s) {
    return (xs32(s) >> 8) * (1.0f / 16777216.0f);
}

static inline int rng_int(uint32_t* s, int n) {
    return (int)(xs32(s) % (uint32_t)n);
}

static float sample_field(const float* field, float x, float y) {
    x = fclamp(x, 0.0f, (float)(DOOM_W - 1));
    y = fclamp(y, 0.0f, (float)(DOOM_H - 1));
    const int x0 = (int)floorf(x);
    const int y0 = (int)floorf(y);
    const int x1 = iclamp(x0 + 1, 0, DOOM_W - 1);
    const int y1 = iclamp(y0 + 1, 0, DOOM_H - 1);
    const float tx = x - (float)x0;
    const float ty = y - (float)y0;
    const float a = field[y0 * DOOM_W + x0];
    const float b = field[y0 * DOOM_W + x1];
    const float c = field[y1 * DOOM_W + x0];
    const float d = field[y1 * DOOM_W + x1];
    return (a + (b - a) * tx) * (1.0f - ty) +
           (c + (d - c) * tx) * ty;
}

static void set_velocity_boundaries(doom_fire_t* f) {
    for (int y = 0; y < DOOM_H; ++y) {
        f->u[y * DOOM_W] = 0.0f;
        f->u[y * DOOM_W + DOOM_W - 1] = 0.0f;
    }
    for (int x = 0; x < DOOM_W; ++x) {
        f->v[(DOOM_H - 1) * DOOM_W + x] = 0.0f;
        if (f->v[x] > 0.0f) f->v[x] = 0.0f;
    }
}

void doom_fire_init(doom_fire_t* f, uint32_t seed) {
    memset(f, 0, sizeof(*f));
    f->intensity = 79.0f;
    f->rng = seed == 0 ? 1u : seed;
}

void doom_fire_reset(doom_fire_t* f) {
    memset(f->embers, 0, sizeof(f->embers));
    memset(f->heat, 0, sizeof(f->heat));
    memset(f->next_heat, 0, sizeof(f->next_heat));
    memset(f->emission, 0, sizeof(f->emission));
    memset(f->next_emission, 0, sizeof(f->next_emission));
    memset(f->fuel, 0, sizeof(f->fuel));
    memset(f->next_fuel, 0, sizeof(f->next_fuel));
    memset(f->u, 0, sizeof(f->u));
    memset(f->v, 0, sizeof(f->v));
    memset(f->work_u, 0, sizeof(f->work_u));
    memset(f->work_v, 0, sizeof(f->work_v));
    memset(f->pressure, 0, sizeof(f->pressure));
    memset(f->divergence, 0, sizeof(f->divergence));
    memset(f->curl, 0, sizeof(f->curl));

    for (int y = DOOM_H - 8; y < DOOM_H - 1; ++y) {
        for (int x = 2; x < DOOM_W - 2; ++x) {
            const int idx = y * DOOM_W + x;
            f->u[idx] = (rng_f01(&f->rng) - 0.5f) * 0.12f;
            f->v[idx] = -rng_f01(&f->rng) * 0.10f;
        }
    }
}

const float* doom_fire_heat(const doom_fire_t* f) { return f->emission; }

static void inject_fuel(doom_fire_t* f, float* heat, float* fuel,
                        float* emission, uint32_t t_ms) {
    const float t = (float)t_ms * 0.001f;
    for (int y = DOOM_H - 4; y < DOOM_H; ++y) {
        const float row = (float)(y - (DOOM_H - 4)) / 3.0f;
        const float vertical = row * row * (3.0f - 2.0f * row);
        for (int x = 0; x < DOOM_W; ++x) {
            const int edge_distance = x < DOOM_W - 1 - x ? x : DOOM_W - 1 - x;
            const float edge = fclamp((float)edge_distance / 4.0f, 0.0f, 1.0f);
            const float broad = sinf((float)x * 0.17f + t * 1.35f);
            const float detail = sinf((float)x * 0.43f - t * 2.10f);
            const float slow = sinf((float)x * 0.071f + t * 0.63f);
            const float source = fclamp(0.50f + broad * 0.27f +
                                            detail * 0.16f + slow * 0.09f,
                                        0.10f, 1.0f);
            const int idx = y * DOOM_W + x;
            const float source_fuel = edge * source * vertical;
            const float source_heat =
                f->intensity * 0.72f * edge * source * vertical;
            const float source_emission = f->intensity * 0.55f * edge *
                                          source * source * vertical;
            if (source_fuel > fuel[idx]) fuel[idx] = source_fuel;
            if (source_heat > heat[idx]) heat[idx] = source_heat;
            const float emission_blend = vertical * 0.48f;
            emission[idx] +=
                (source_emission - emission[idx]) * emission_blend;
        }
    }
}

static void buoyancy_direction(float gravity_x, float gravity_y,
                               float* buoyancy_x, float* buoyancy_y) {
    gravity_x = fclamp(gravity_x, -1.5f, 1.5f);
    gravity_y = fclamp(gravity_y, -1.5f, 1.5f);
    float magnitude = sqrtf(gravity_x * gravity_x + gravity_y * gravity_y);
    if (magnitude < 0.25f) {
        gravity_x = 0.0f;
        gravity_y = -1.0f;
        magnitude = 1.0f;
    }
    const float gravity_buoyancy_x = -gravity_x / magnitude;
    const float gravity_buoyancy_y = gravity_y / magnitude;

    // Combustion keeps ejecting hot gas away from the bottom burner even when
    // gravity points sideways. IMU gravity bends that jet instead of replacing it.
    const float mixed_x = gravity_buoyancy_x * 0.55f;
    const float mixed_y = -0.45f + gravity_buoyancy_y * 0.55f;
    const float mixed_magnitude = sqrtf(mixed_x * mixed_x + mixed_y * mixed_y);
    if (mixed_magnitude < 0.001f) {
        *buoyancy_x = 0.0f;
        *buoyancy_y = -1.0f;
        return;
    }

    *buoyancy_x = mixed_x / mixed_magnitude;
    *buoyancy_y = mixed_y / mixed_magnitude;

    // Keep a visible outward component near the upside-down singularity.
    if (*buoyancy_y > -0.30f) {
        if (fabsf(*buoyancy_x) < 0.001f) {
            *buoyancy_x = 0.0f;
            *buoyancy_y = -1.0f;
        } else {
            *buoyancy_x = *buoyancy_x < 0.0f ? -0.95394f : 0.95394f;
            *buoyancy_y = -0.30f;
        }
    }
}

static void apply_forces(doom_fire_t* f, float buoyancy_x, float buoyancy_y,
                         uint32_t t_ms) {
    const float t = (float)t_ms * 0.001f;
    for (int y = 1; y < DOOM_H - 1; ++y) {
        for (int x = 1; x < DOOM_W - 1; ++x) {
            const int idx = y * DOOM_W + x;
            const float hot = fclamp(f->heat[idx] / (float)DOOM_HEAT_MAX,
                                     0.0f, 1.0f);
            const float active = 0.20f + hot * 0.80f;
            const float coherent = sinf((float)x * 0.105f + t * 1.70f +
                                        sinf((float)y * 0.19f) * 1.4f);
            const float cross = sinf((float)x * 0.31f - t * 1.13f +
                                     (float)y * 0.07f);
            f->u[idx] += (coherent * 0.025f + cross * 0.012f) * active;
            const float buoyancy = hot * 0.115f + f->fuel[idx] * 0.024f;
            f->u[idx] += buoyancy_x * buoyancy;
            f->v[idx] += buoyancy_y * buoyancy;
        }
    }

    for (int y = 1; y < DOOM_H - 1; ++y) {
        for (int x = 1; x < DOOM_W - 1; ++x) {
            const int idx = y * DOOM_W + x;
            const float dvdx = (f->v[idx + 1] - f->v[idx - 1]) * 0.5f;
            const float dudy =
                (f->u[idx + DOOM_W] - f->u[idx - DOOM_W]) * 0.5f;
            f->curl[idx] = dvdx - dudy;
        }
    }

    for (int y = 2; y < DOOM_H - 2; ++y) {
        for (int x = 2; x < DOOM_W - 2; ++x) {
            const int idx = y * DOOM_W + x;
            float nx = fabsf(f->curl[idx + 1]) - fabsf(f->curl[idx - 1]);
            float ny = fabsf(f->curl[idx + DOOM_W]) -
                       fabsf(f->curl[idx - DOOM_W]);
            const float length = sqrtf(nx * nx + ny * ny) + 1.0e-5f;
            nx /= length;
            ny /= length;
            const float hot = fclamp(f->heat[idx] / (float)DOOM_HEAT_MAX,
                                     0.0f, 1.0f);
            const float force = f->curl[idx] * (0.15f + hot * 0.28f);
            f->u[idx] += ny * force;
            f->v[idx] -= nx * force;
        }
    }
    set_velocity_boundaries(f);
}

static void advect_velocity(doom_fire_t* f) {
    memset(f->work_u, 0, sizeof(f->work_u));
    memset(f->work_v, 0, sizeof(f->work_v));
    for (int y = 1; y < DOOM_H - 1; ++y) {
        for (int x = 1; x < DOOM_W - 1; ++x) {
            const int idx = y * DOOM_W + x;
            const float back_x = (float)x - f->u[idx] * 0.82f;
            const float back_y = (float)y - f->v[idx] * 0.82f;
            f->work_u[idx] = sample_field(f->u, back_x, back_y) * 0.997f;
            f->work_v[idx] = sample_field(f->v, back_x, back_y) * 0.997f;
        }
    }
    memcpy(f->u, f->work_u, sizeof(f->u));
    memcpy(f->v, f->work_v, sizeof(f->v));
    set_velocity_boundaries(f);
}

static void project_velocity(doom_fire_t* f) {
    memset(f->pressure, 0, sizeof(f->pressure));
    memset(f->divergence, 0, sizeof(f->divergence));
    for (int y = 1; y < DOOM_H - 1; ++y) {
        for (int x = 1; x < DOOM_W - 1; ++x) {
            const int idx = y * DOOM_W + x;
            f->divergence[idx] = 0.5f *
                (f->u[idx + 1] - f->u[idx - 1] +
                 f->v[idx + DOOM_W] - f->v[idx - DOOM_W]);
        }
    }

    for (int iter = 0; iter < PRESSURE_ITERS; ++iter) {
        memset(f->work_u, 0, sizeof(f->work_u));
        for (int y = 1; y < DOOM_H - 1; ++y) {
            for (int x = 1; x < DOOM_W - 1; ++x) {
                const int idx = y * DOOM_W + x;
                f->work_u[idx] =
                    (f->pressure[idx - 1] + f->pressure[idx + 1] +
                     f->pressure[idx - DOOM_W] +
                     f->pressure[idx + DOOM_W] - f->divergence[idx]) *
                    0.25f;
            }
        }
        memcpy(f->pressure, f->work_u, sizeof(f->pressure));
    }

    for (int y = 1; y < DOOM_H - 1; ++y) {
        for (int x = 1; x < DOOM_W - 1; ++x) {
            const int idx = y * DOOM_W + x;
            f->u[idx] -=
                (f->pressure[idx + 1] - f->pressure[idx - 1]) * 0.5f;
            f->v[idx] -=
                (f->pressure[idx + DOOM_W] -
                 f->pressure[idx - DOOM_W]) * 0.5f;
        }
    }
    set_velocity_boundaries(f);
}

static void advect_combustion(doom_fire_t* f) {
    memset(f->next_heat, 0, sizeof(f->next_heat));
    memset(f->next_fuel, 0, sizeof(f->next_fuel));
    memset(f->next_emission, 0, sizeof(f->next_emission));
    for (int y = 0; y < DOOM_H; ++y) {
        const float height = 1.0f - (float)y / (float)(DOOM_H - 1);
        for (int x = 0; x < DOOM_W; ++x) {
            const int idx = y * DOOM_W + x;
            const float back_x = (float)x - f->u[idx] * 1.02f;
            const float back_y = (float)y - f->v[idx] * 1.02f;

            const float h0 = sample_field(f->heat, back_x, back_y);
            const float heat_neighbors =
                sample_field(f->heat, back_x - 1.0f, back_y) +
                sample_field(f->heat, back_x + 1.0f, back_y) +
                sample_field(f->heat, back_x, back_y - 1.0f) +
                sample_field(f->heat, back_x, back_y + 1.0f);
            float heat = h0 * 0.86f + heat_neighbors * 0.035f;

            const float f0 = sample_field(f->fuel, back_x, back_y);
            const float fuel_neighbors =
                sample_field(f->fuel, back_x - 1.0f, back_y) +
                sample_field(f->fuel, back_x + 1.0f, back_y) +
                sample_field(f->fuel, back_x, back_y - 1.0f) +
                sample_field(f->fuel, back_x, back_y + 1.0f);
            float fuel = f0 * 0.92f + fuel_neighbors * 0.02f;

            heat = fmax2(0.0f, heat * 0.974f - (0.36f + height * 0.58f));
            fuel *= 0.988f;
            float burn = 0.0f;
            if (heat > 9.0f && fuel > 0.004f) {
                const float hot = fclamp(heat / (float)DOOM_HEAT_MAX,
                                         0.0f, 1.0f);
                burn = fmin2(fuel, 0.025f + hot * 0.036f);
                fuel -= burn;
                heat += burn * 24.0f;
            }

            const float e0 = sample_field(f->emission, back_x, back_y);
            const float emission_neighbors =
                sample_field(f->emission, back_x - 1.0f, back_y) +
                sample_field(f->emission, back_x + 1.0f, back_y) +
                sample_field(f->emission, back_x, back_y - 1.0f) +
                sample_field(f->emission, back_x, back_y + 1.0f);
            const float emission =
                (e0 * 0.92f + emission_neighbors * 0.015f) * 0.98f +
                burn * 390.0f;

            f->next_heat[idx] = fclamp(heat, 0.0f, (float)DOOM_HEAT_MAX);
            f->next_fuel[idx] = fclamp(fuel, 0.0f, 1.0f);
            f->next_emission[idx] =
                fclamp(emission, 0.0f, (float)DOOM_HEAT_MAX);
        }
    }
}

static int spawn_ember(doom_fire_t* f, float origin_x) {
    for (int i = 0; i < DOOM_MAX_EMBERS; ++i) {
        doom_ember_t* ember = &f->embers[i];
        if (ember->active) continue;
        ember->x = fclamp(origin_x + (rng_f01(&f->rng) - 0.5f) * 4.0f,
                          2.0f, (float)(DOOM_W - 3));
        ember->y = (float)(DOOM_H - 8 - rng_int(&f->rng, 4));
        ember->vx = (rng_f01(&f->rng) - 0.5f) * 0.24f;
        ember->vy = -0.55f - rng_f01(&f->rng) * 0.55f;
        ember->heat = 68.0f + rng_f01(&f->rng) * 12.0f;
        ember->active = 1;
        return 1;
    }
    return 0;
}

static void update_embers(doom_fire_t* f, float buoyancy_x, float buoyancy_y) {
    if (rng_f01(&f->rng) < 0.25f) {
        const float origin = 4.0f + rng_f01(&f->rng) * (float)(DOOM_W - 9);
        spawn_ember(f, origin);
        if (rng_f01(&f->rng) < 0.10f) spawn_ember(f, origin);
    }

    for (int i = 0; i < DOOM_MAX_EMBERS; ++i) {
        doom_ember_t* ember = &f->embers[i];
        if (!ember->active) continue;
        const float flow_u = sample_field(f->u, ember->x, ember->y);
        const float flow_v = sample_field(f->v, ember->x, ember->y);
        ember->vx += (flow_u - ember->vx) * 0.10f + buoyancy_x * 0.010f;
        ember->vy += (flow_v - ember->vy) * 0.08f + buoyancy_y * 0.010f;
        ember->x += ember->vx;
        ember->y += ember->vy;
        ember->heat -= 0.95f + rng_f01(&f->rng) * 0.32f;
        if (ember->x < 1.0f || ember->x >= (float)(DOOM_W - 1) ||
            ember->y < 1.0f || ember->heat < 8.0f) {
            ember->active = 0;
        }
    }
}

void doom_fire_step(doom_fire_t* f, float gravity_x, float gravity_y,
                    uint32_t t_ms) {
    float buoyancy_x = 0.0f;
    float buoyancy_y = -1.0f;
    buoyancy_direction(gravity_x, gravity_y, &buoyancy_x, &buoyancy_y);
    inject_fuel(f, f->heat, f->fuel, f->emission, t_ms);
    apply_forces(f, buoyancy_x, buoyancy_y, t_ms);
    advect_velocity(f);
    project_velocity(f);
    advect_combustion(f);
    inject_fuel(f, f->next_heat, f->next_fuel, f->next_emission, t_ms);
    memcpy(f->heat, f->next_heat, sizeof(f->heat));
    memcpy(f->fuel, f->next_fuel, sizeof(f->fuel));
    memcpy(f->emission, f->next_emission, sizeof(f->emission));
    update_embers(f, buoyancy_x, buoyancy_y);
}
