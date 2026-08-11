#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DOOM_W 96
#define DOOM_H 52
#define DOOM_N (DOOM_W * DOOM_H)

#define DOOM_HEAT_MAX 80
#define DOOM_HEAT_LEVELS (DOOM_HEAT_MAX + 1)
#define DOOM_MAX_EMBERS 24

typedef struct {
    float x;
    float y;
    float vx;
    float vy;
    float heat;
    uint8_t active;
} doom_ember_t;

typedef struct {
    float intensity;
    uint32_t rng;
    doom_ember_t embers[DOOM_MAX_EMBERS];
    float heat[DOOM_N];
    float next_heat[DOOM_N];
    float emission[DOOM_N];
    float next_emission[DOOM_N];
    float fuel[DOOM_N];
    float next_fuel[DOOM_N];
    float u[DOOM_N];
    float v[DOOM_N];
    float work_u[DOOM_N];
    float work_v[DOOM_N];
    float pressure[DOOM_N];
    float divergence[DOOM_N];
    float curl[DOOM_N];
} doom_fire_t;

void doom_fire_init(doom_fire_t* f, uint32_t seed);
void doom_fire_reset(doom_fire_t* f);
void doom_fire_step(doom_fire_t* f, float gravity_x, float gravity_y,
                    uint32_t t_ms);
const float* doom_fire_heat(const doom_fire_t* f);

#ifdef __cplusplus
}
#endif
