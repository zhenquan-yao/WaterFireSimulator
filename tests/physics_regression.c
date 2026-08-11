#include "../src/app/flip.h"
#include "../src/app/doom_fire.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int water_settles(void) {
    FlipFluid* water = flip_create(2.0f, 1.0f, 48, 0.40f);
    if (!water) {
        fprintf(stderr, "failed to create water simulation\n");
        return 0;
    }

    flip_set_gravity_scale(water, 9.81f);
    flip_set_solver_quality(water, 4, 28, 0.42f);
    flip_set_velocity_damping(water, 0.995f, 0.0025f);

    const int grid_size = flip_grid_width(water) * flip_grid_height(water);
    float* grid = (float*)calloc((size_t)grid_size, sizeof(float));
    if (!grid) {
        flip_destroy(water);
        return 0;
    }
    const int initial_particles = flip_particle_count(water);

    for (int frame = 0; frame < 5; ++frame) {
        for (int substep = 0; substep < 2; ++substep) {
            flip_step(water, 1.0f / 60.0f, 0.0f, -1.0f);
        }
    }
    flip_get_led_grid(water, grid);
    double initial_visual_mass = 0.0;
    for (int i = 0; i < grid_size; ++i) initial_visual_mass += grid[i];

    for (int frame = 5; frame < 300; ++frame) {
        for (int substep = 0; substep < 2; ++substep) {
            flip_step(water, 1.0f / 60.0f, 0.0f, -1.0f);
        }
    }
    flip_get_led_grid(water, grid);
    double settled_visual_mass = 0.0;
    for (int i = 0; i < grid_size; ++i) settled_visual_mass += grid[i];
    float* settled_grid = (float*)malloc(sizeof(float) * (size_t)grid_size);
    if (!settled_grid) {
        free(grid);
        flip_destroy(water);
        return 0;
    }
    memcpy(settled_grid, grid, sizeof(float) * (size_t)grid_size);
    const double visual_mass_ratio = initial_visual_mass > 0.0
                                         ? settled_visual_mass / initial_visual_mass
                                         : 0.0;
    printf("water visual mass initial=%.1f settled=%.1f ratio=%.3f particles=%d\n",
           initial_visual_mass, settled_visual_mass, visual_mass_ratio,
           initial_particles);

    const float settled_motion = flip_motion_level(water);
    printf("water rms after 10 s: %.6f\n", settled_motion);
    for (int frame = 0; frame < 30; ++frame) {
        for (int substep = 0; substep < 2; ++substep) {
            flip_step(water, 1.0f / 60.0f, 0.0f, -1.0f);
        }
    }
    flip_get_led_grid(water, grid);
    double visual_delta = 0.0;
    for (int i = 0; i < grid_size; ++i) {
        visual_delta += fabs((double)grid[i] - (double)settled_grid[i]);
    }
    visual_delta /= (double)grid_size;
    printf("water visual delta over 1 s: %.6f\n", visual_delta);
    const int settled = settled_motion < 0.20f && visual_delta < 0.55;

    flip_stop(water);
    const int stopped = flip_motion_level(water) == 0.0f;

    for (int frame = 0; frame < 15; ++frame) {
        for (int substep = 0; substep < 2; ++substep) {
            flip_step(water, 1.0f / 60.0f, 0.8f, -0.6f);
        }
    }
    const float wake_motion = flip_motion_level(water);
    printf("water rms after tilt: %.6f\n", wake_motion);
    for (int frame = 0; frame < 6; ++frame) {
        for (int substep = 0; substep < 2; ++substep) {
            flip_step(water, 1.0f / 60.0f, 0.0f, -1.0f);
        }
    }
    const float coast_motion = flip_motion_level(water);
    printf("water rms while coasting: %.6f\n", coast_motion);
    const int particle_count_preserved =
        flip_particle_count(water) == initial_particles;
    free(settled_grid);
    free(grid);
    flip_destroy(water);
    return settled && stopped && wake_motion > 0.08f &&
           coast_motion > wake_motion * 0.35f &&
           particle_count_preserved && visual_mass_ratio > 0.78 &&
           visual_mass_ratio < 1.22;
}

static int fire_has_natural_extent(void) {
    doom_fire_t fire;
    doom_fire_init(&fire, 0x57415445U);
    doom_fire_reset(&fire);

    for (uint32_t frame = 0; frame < 300; ++frame) {
        doom_fire_step(&fire, 0.0f, -1.0f, frame * 33U);
    }

    const float* heat = doom_fire_heat(&fire);
    double weighted_x = 0.0;
    double total_heat = 0.0;
    int hot_count = 0;
    int min_x = DOOM_W;
    int max_x = -1;
    int min_y = DOOM_H;
    double lateral_flow = 0.0;
    int flowing_cells = 0;
    for (int y = 0; y < DOOM_H; ++y) {
        for (int x = 0; x < DOOM_W; ++x) {
            const float value = heat[y * DOOM_W + x];
            weighted_x += (double)x * value;
            total_heat += value;
            if (value > 4.0f) {
                ++hot_count;
                lateral_flow += fabs((double)fire.u[y * DOOM_W + x]);
                ++flowing_cells;
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (y < min_y) min_y = y;
            }
        }
    }

    int active_embers = 0;
    int raised_embers = 0;
    for (int i = 0; i < DOOM_MAX_EMBERS; ++i) {
        if (!fire.embers[i].active) continue;
        ++active_embers;
        if (fire.embers[i].y < (float)DOOM_H * 0.70f) ++raised_embers;
    }

    const double center_x = total_heat > 0.0 ? weighted_x / total_heat : -1.0;
    const double mean_lateral_flow =
        flowing_cells > 0 ? lateral_flow / (double)flowing_cells : 0.0;
    const int width = max_x >= min_x ? max_x - min_x + 1 : 0;
    printf("fire hot=%d width=%d top=%d embers=%d raised=%d lateral=%.4f center=%.2f\n",
           hot_count, width, min_y, active_embers, raised_embers,
           mean_lateral_flow, center_x);
    return hot_count > 700 && width > 72 && min_y < 32 &&
           active_embers >= 6 && raised_embers >= 2 && mean_lateral_flow > 0.01 &&
           fabs(center_x - (DOOM_W - 1) * 0.5) < 14.0;
}

static void fire_mean_flow(float gravity_x, float gravity_y,
                           double* mean_horizontal, double* mean_vertical) {
    doom_fire_t fire;
    doom_fire_init(&fire, 0x464c4f57U);
    doom_fire_reset(&fire);
    for (uint32_t frame = 0; frame < 180; ++frame) {
        doom_fire_step(&fire, gravity_x, gravity_y, frame * 33U);
    }

    double weighted_flow = 0.0;
    double weighted_vertical_flow = 0.0;
    double total_heat = 0.0;
    const float* heat = doom_fire_heat(&fire);
    for (int i = 0; i < DOOM_N; ++i) {
        if (heat[i] <= 4.0f) continue;
        weighted_flow += (double)fire.u[i] * heat[i];
        weighted_vertical_flow += (double)fire.v[i] * heat[i];
        total_heat += heat[i];
    }
    *mean_horizontal = total_heat > 0.0 ? weighted_flow / total_heat : 0.0;
    *mean_vertical = total_heat > 0.0 ? weighted_vertical_flow / total_heat : 0.0;
}

int main(void) {
    const int water_ok = water_settles();
    const int fire_ok = fire_has_natural_extent();
    double right_gravity_flow = 0.0;
    double right_vertical_flow = 0.0;
    double left_gravity_flow = 0.0;
    double left_vertical_flow = 0.0;
    double inverted_horizontal_flow = 0.0;
    double inverted_vertical_flow = 0.0;
    fire_mean_flow(1.0f, 0.0f, &right_gravity_flow, &right_vertical_flow);
    fire_mean_flow(-1.0f, 0.0f, &left_gravity_flow, &left_vertical_flow);
    fire_mean_flow(0.0f, 1.0f, &inverted_horizontal_flow,
                   &inverted_vertical_flow);
    const int fire_direction_ok = right_gravity_flow < -0.03 &&
                                  left_gravity_flow > 0.03 &&
                                  right_vertical_flow < -0.03 &&
                                  left_vertical_flow < -0.03 &&
                                  inverted_vertical_flow < -0.03;
    printf("fire directional flow right=(%.4f, %.4f) left=(%.4f, %.4f) "
           "inverted=(%.4f, %.4f)\n",
           right_gravity_flow, right_vertical_flow,
           left_gravity_flow, left_vertical_flow,
           inverted_horizontal_flow, inverted_vertical_flow);
    if (!water_ok || !fire_ok || !fire_direction_ok) {
        fprintf(stderr,
                "physics regression failed: water=%d fire=%d direction=%d\n",
                water_ok, fire_ok, fire_direction_ok);
        return 1;
    }
    return 0;
}
