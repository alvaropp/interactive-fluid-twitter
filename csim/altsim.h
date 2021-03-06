#ifndef ALTSIM_H
#define ALTSIM_H

#include <stdio.h>
#include <omp.h>

const int num_loops = 3;
const int accuracy_mode = 1;
const int pressure_solve_steps = 20;

inline double advect_sample(const double const * v, int Ny, double s, double t) {
    return (1 - s) * ((1 - t) * v[0] + t * v[1])
        + s * ((1 - t) * v[Ny] + t * v[Ny + 1]);
}

void C_advect_velocity(
    double * v,
    const double * const v0,
    const unsigned char * bound,
    int * advect_indexes,
    double * advect_lerps,
    const int Nx,
    const int Ny,
    const double dx,
    const double dy,
    const double dt) {

    int x, y, idx, xi, yi, iidx;
    int vyidx = Nx * Ny;
    float xa, ya, s, t;

    // memset(advect_indexes, 0, 2 * Nx * Ny * sizeof(int));
    // memset(advect_lerps, 0, 2 * Nx * Ny * sizeof(double));

    #pragma omp for schedule(auto)
    for(x = 0; x < Nx; ++x) {
        for(y = 0; y < Ny; ++y) {
            idx = y + x * Ny;

            xa = (double)x - dt * v0[0 + idx] / dx;
            ya = (double)y - dt * v0[vyidx + idx] / dy;

            xa = xa < 0.0 ? 0.0 : (xa >= Nx - 1.01) ? (Nx - 1.01) : xa;
            ya = ya < 0.0 ? 0.0 : (ya >= Ny - 1.01) ? (Ny - 1.01) : ya;

            xi = (int)xa;
            yi = (int)ya;

            s = xa - (double)xi;
            t = ya - (double)yi;

            advect_indexes[0 + idx] = xi;
            advect_indexes[vyidx + idx] = yi;
            advect_lerps[0 + idx] = s;
            advect_lerps[vyidx + idx] = t;

            if(!bound[idx]) {
                iidx = yi + xi * Ny;
                v[0 + idx] = advect_sample(v0 + iidx, Ny, s, t);
                v[vyidx + idx] = advect_sample(v0 + vyidx + iidx, Ny, s, t);
            } else {
                v[0 + idx] = v0[0 + idx];
                v[vyidx + idx] = v0[vyidx + idx];
            }

        }
    }

}

void C_apply_advection(double * d, const double * const d0,
                      const unsigned char * bound,
                      int * advect_indexes, double * advect_lerps,
                      const int Nx, const int Ny) {

    int vyidx = Nx * Ny;
    int x, y, idx, iidx;

    #pragma omp for schedule(auto)
    for(x = 0; x < Nx; ++x) {
        for(y = 0; y < Ny; ++y) {
            idx = y + x * Ny;
            if(!bound[idx]) {
                iidx = advect_indexes[vyidx + idx] + advect_indexes[idx] * Ny;
                d[0 + idx] = advect_sample(d0 + iidx, Ny, advect_lerps[idx],
                    advect_lerps[vyidx + idx]);
            } else {
                d[0 + idx] *= 0.9;
            }

        }
    }
}

void C_pressure_solve(
    double * pressure,
    double * pressure_buffer,
    double const * const div,
    unsigned char const * const bound,
    int const Nx,
    int const Ny,
    double const dx,
    double const dy) {

    //memset(pressure, 0, Nx * Ny * sizeof(double));
    //memset(pressure_buffer, 0, Nx * Ny * sizeof(double));

    int x, y, k, idx;
    double * temp = 0;

    // shouldn't have any pressure inside solids
    #pragma omp for schedule(auto)
    for(x = 0; x < Nx * Ny; ++x) {
        pressure[x] *= bound[x] ? 0.9 : 1.0;
    }
    /*
    #pragma omp parallel for schedule(auto) private(y, idx)
    for(x = 0; x < Nx * Ny; ++x) {
        pressure[x] = 0.0;
    }
    */

    // make sure this is a multiple of 2 steps
    for(k = 0; k < pressure_solve_steps; ++k) {
        #pragma omp for schedule(auto)
        for(x = 0; x < Nx; ++x) {
            pressure_buffer[x * Ny] = 0;
            pressure_buffer[(x + 1) * Ny - 1] = 0;
        }

        #pragma omp for schedule(auto)
        for(y = 0; y < Ny; ++y) {
            pressure_buffer[y] = 0;
            pressure_buffer[y + Ny * (Nx - 1)] = 0;
        }

        #pragma omp for schedule(auto)
        for(x = 1; x < Nx - 1; ++x) {
            for(y = 1; y < Ny - 1; ++y) {
                idx = y + x * Ny;
                pressure_buffer[idx] = (1.0 / 4) * (
                    (bound[idx - 1] ? pressure[idx] : pressure[idx - 1])
                    + (bound[idx + 1] ? pressure[idx] : pressure[idx + 1])
                    + (bound[idx - Ny] ? pressure[idx] : pressure[idx - Ny])
                    + (bound[idx + Ny] ? pressure[idx] : pressure[idx + Ny])
                        - dx * dy * div[idx]);
            }
        }

        {
            temp = pressure_buffer;
            pressure_buffer = pressure;
            pressure = temp;
        }

        #pragma omp barrier
    }
}

void C_divergence(
    double * div,
    const double * const v,
    const unsigned char * bound,
    int const Nx,
    int const Ny,
    const double dx,
    const double dy) {

    int x, y, idx;
    int vyidx = Nx * Ny;

    #pragma omp for schedule(auto)
    for(x = 1; x < Nx - 1; ++x) {
        for(y = 1; y < Ny - 1; ++y) {
            idx = y + x * Ny;
            div[idx] = (bound[idx + Ny] ? 0.0 : v[idx + Ny] / (2 * dx))
                - (bound[idx - Ny] ? 0.0 : v[idx - Ny] / (2 * dx))
                + (bound[idx + 1] ? 0.0 : v[vyidx + idx + 1] / (2 * dy))
                - (bound[idx - 1] ? 0.0 : v[vyidx + idx - 1] / (2 * dy));
        }
    }
}

void C_sub_gradient(
    double * v,
    const double * const v0,
    const double * const p,
    const unsigned char * bound,
    const int Nx,
    const int Ny,
    const double dx,
    const double dy) {

    int x, y, idx;
    int vyidx = Nx * Ny;

    #pragma omp for schedule(auto)
    for(x = 1; x < Nx - 1; ++x) {
        for(y = 1; y < Ny - 1; ++y) {
            idx = y + x * Ny;
            v[idx] = bound[idx] ? v0[idx] : v0[idx] - 1 / (2 * dx) * (p[idx + Ny] - p[idx - Ny]);
            v[vyidx + idx] = bound[idx] ? v0[vyidx + idx] : v0[vyidx + idx] - 1 / (2 * dy) * (p[idx + 1] - p[idx - 1]);
        }
    }
}

void C_enforce_slip(
    double * v,
    const unsigned char * bound,
    const int Nx,
    const int Ny) {

    int x, y, idx;
    int vyidx = Nx * Ny;

    /*
    #pragma omp parallel for schedule(auto) private(y, idx)
    for(x = 1; x < Nx - 1; ++x) {
        for(y = 1; y < Ny - 1; ++y) {
            idx = y + x * Ny;
            // take x velocity from vertical boundaries
            v[idx] = bound[idx] ? 0.0 : v[idx];

            // take y velocity from horizontal boundaries
            v[vyidx + idx] = bound[idx] ? 0.0 : v[vyidx + idx];
        }
    }
    */

    #pragma omp for schedule(auto)
    for(x = 1; x < Nx - 1; ++x) {
        for(y = 1; y < Ny - 1; ++y) {
            idx = y + x * Ny;
            // take x velocity from vertical boundaries
            v[idx] = bound[idx] ? v[idx] :
                bound[idx + Ny] ? v[idx + Ny] :
                bound[idx - Ny] ? v[idx - Ny] : v[idx];

            // take y velocity from horizontal boundaries
            v[vyidx + idx] = bound[idx] ? v[vyidx + idx] :
                bound[idx + 1] ? v[vyidx + idx + 1] :
                bound[idx - 1] ? v[vyidx + idx - 1] : v[vyidx + idx];
        }
    }

}

void C_step(
    double * v,
    double * vtmp,
    double * vtmp2,
    double * p,
    double * div,
    double * density_arrays,
    const int num_density_arrays,
    const unsigned char * bound,
    int * advect_indexes,
    double * advect_lerps,
    const int Nx,
    const int Ny,
    const double dx,
    const double dy,
    const double dt0
        ) {

    #pragma omp parallel
    {
        int i, j, x, idx;

        double dt = dt0 / num_loops;

        for(i = 0; i < num_loops; ++i) {
            #pragma omp barrier

            if(accuracy_mode == 1) {
                // BFECC
                C_advect_velocity(vtmp2, v, bound, advect_indexes, advect_lerps, Nx, Ny, dx, dy, dt);

                C_advect_velocity(vtmp, vtmp2, bound, advect_indexes, advect_lerps, Nx, Ny, dx, dy, -dt);

                #pragma omp for schedule(auto)
                for(x = 0; x < Nx * Ny * 2; ++x) {
                    vtmp2[x] = 1.5 * v[x] - 0.5 * vtmp[x];
                }

                // Corrected advection
                C_advect_velocity(vtmp, vtmp2, bound, advect_indexes, advect_lerps, Nx, Ny, dx, dy, dt);
            } else {
                // Standard advection
                C_advect_velocity(vtmp, v, bound, advect_indexes, advect_lerps, Nx, Ny, dx, dy, dt);
            }

            // remove divergence
            C_divergence(div, vtmp, bound, Nx, Ny, dx, dy);
            C_pressure_solve(p, vtmp2, div, bound, Nx, Ny, dx, dy);
            C_sub_gradient(v, vtmp, p, bound, Nx, Ny, dx, dy);

            // enforce slip at boundary
            C_enforce_slip(v, bound, Nx, Ny);

            for(j = 0; j < num_density_arrays; ++j) {
                idx = Nx * Ny * j;
                #pragma omp for schedule(auto)
                for(x = 0; x < Nx * Ny; ++x) {
                    vtmp[x] = density_arrays[idx + x];
                }
                C_apply_advection(density_arrays + idx, vtmp, bound, advect_indexes,
                    advect_lerps, Nx, Ny);
            }
        }
    }


}


#endif
