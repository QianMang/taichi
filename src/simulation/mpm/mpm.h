/*******************************************************************************
    Taichi - Physically based Computer Graphics Library

    Copyright (c) 2016 Yuanming Hu <yuanmhu@gmail.com>

    All rights reserved. Use of this source code is governed by
    the MIT license as written in the LICENSE file.
*******************************************************************************/

#pragma once

#include <memory>
#include <vector>
#include <memory.h>
#include <string>
#include <functional>

#include <taichi/visualization/image_buffer.h>
#include <taichi/common/meta.h>
#include <taichi/dynamics/simulation.h>
#include <taichi/math/array_3d.h>
#include <taichi/math/qr_svd.h>
#include <taichi/math/levelset.h>
#include <taichi/system/threading.h>

#include "mpm_scheduler.h"
#include "mpm_particle.h"
#include "mpm_kernel.h"

TC_NAMESPACE_BEGIN

// Supports FLIP?
// #define TC_MPM_WITH_FLIP

template <int DIM>
class MPM : public Simulation<DIM> {
public:
    using Vector = VectorND<DIM, real>;
    using VectorP = VectorND<DIM + 1, real>;
    using VectorI = VectorND<DIM, int>;
    using Vectori = VectorND<DIM, int>;
    using Matrix = MatrixND<DIM, real>;
    using MatrixP = MatrixND<DIM + 1, real>;
    using Kernel = MPMKernel<DIM, mpm_kernel_order>;
    static const int D = DIM;
    static const int kernel_size;
    using Particle = MPMParticle<DIM>;
    std::vector<MPMParticle<DIM> *> particles; // for (copy) efficiency, we do not use smart pointers here
    ArrayND<DIM, Vector> grid_velocity;
    ArrayND<DIM, real> grid_mass;
    ArrayND<DIM, VectorP> grid_velocity_and_mass;
#ifdef TC_MPM_WITH_FLIP
    ArrayND<DIM, Vector> grid_velocity_backup;
#endif
    ArrayND<DIM, Spinlock> grid_locks;
    VectorI res;
    Vector gravity;
    bool apic;
    bool async;
    real delta_x;
    real inv_delta_x;
    real affine_damping;
    real base_delta_t;
    real maximum_delta_t;
    real cfl;
    real strength_dt_mul;
    real request_t = 0.0f;
    bool use_mpi;
    int mpi_world_size;
    int mpi_world_rank;
    int64 current_t_int = 0;
    int64 original_t_int_increment;
    int64 t_int_increment;
    int64 old_t_int;
    MPMScheduler<DIM> scheduler;
    static const int grid_block_size = 8;
    bool mpi_initialized;

    bool test() const override;

    void estimate_volume() {}

    void resample();

    void grid_backup_velocity() {
#ifdef TC_MPM_WITH_FLIP
        grid_velocity_backup = grid_velocity;
#endif
    }

    void calculate_force();

    void rasterize(real delta_t);

    void normalize_grid();

    void grid_apply_boundary_conditions(const DynamicLevelSet<D> &levelset, real t);

    void grid_apply_external_force(Vector acc, real delta_t) {
        for (auto &ind : grid_mass.get_region()) {
            if (grid_mass[ind] > 0) // Do not use EPS here!!
                grid_velocity[ind] += delta_t * acc;
        }
    }

    void particle_collision_resolution(real t);

    void substep();

    template <typename T>
    void parallel_for_each_particle(const T &target) {
        ThreadedTaskManager::run((int)particles.size(), this->num_threads, [&](int i) {
            target(*particles[i]);
        });
    }

    template <typename T>
    void parallel_for_each_active_particle(const T &target) {
        ThreadedTaskManager::run((int)scheduler.get_active_particles().size(), this->num_threads, [&](int i) {
            target(*scheduler.get_active_particles()[i]);
        });
    }

public:

    MPM() {}

    virtual void initialize(const Config &config) override;

    virtual void add_particles(const Config &config) override;

    virtual void step(real dt) override {
        if (dt < 0) {
            substep();
            request_t = this->current_t;
        } else {
            request_t += dt;
            while (this->current_t + base_delta_t < request_t) {
                substep();
            }
            P(t_int_increment * base_delta_t);
        }
    }

    void synchronize_particles();

    void finalize();

    void clear_particles_outside();

    std::vector<RenderParticle> get_render_particles() const override;

    int get_mpi_world_rank() const override {
        return mpi_world_rank;
    }

    void clear_boundary_particles();

    ~MPM();
};

TC_NAMESPACE_END

