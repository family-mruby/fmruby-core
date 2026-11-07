/*
 * crt_warp.c - the CRT look's curve in C (crt_warp.h). Written as crt.js's
 * warpToSource / warpToDisplay are, operation for operation, so that both
 * give the same doubles.
 */
#include "crt_warp.h"

#include <math.h>

#define CURVE_MAX 0.25

double crt_curve_coef(int curve)
{
    if (curve < 0) curve = 0;
    if (curve > 100) curve = 100;
    return (double)curve / 100 * CURVE_MAX;
}

void crt_warp_to_source(double u, double v, int curve, double *x, double *y)
{
    double c = crt_curve_coef(curve);
    if (c == 0) {
        *x = u;
        *y = v;
        return;
    }
    double dx = u * 2 - 1, dy = v * 2 - 1;
    double k = 1 + c * (dx * dx + dy * dy);
    *x = (dx * k + 1) / 2;
    *y = (dy * k + 1) / 2;
}

void crt_warp_to_display(double x, double y, int curve, double *u, double *v)
{
    double c = crt_curve_coef(curve);
    if (c == 0) {
        *u = x;
        *v = y;
        return;
    }
    double sx = x * 2 - 1, sy = y * 2 - 1;
    double s = hypot(sx, sy);
    if (s == 0) {
        *u = 0.5;
        *v = 0.5;
        return;
    }
    double r = s;
    for (int i = 0; i < 12; i++) {
        double f = r + c * r * r * r - s;
        r -= f / (1 + 3 * c * r * r);
    }
    double t = r / s;
    *u = (sx * t + 1) / 2;
    *v = (sy * t + 1) / 2;
}

bool crt_inside(double x, double y)
{
    return x >= 0 && x < 1 && y >= 0 && y < 1;
}

double crt_fine_weight(double scale, double lo, double hi)
{
    double t = (scale - lo) / (hi - lo);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return t * t * (3 - 2 * t);
}
