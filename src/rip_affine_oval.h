/* Internal geometry for DLL slots 8, 10, 11, 83 and 84 (D-31).
 * C is the centre; A and B are endpoints of two conjugate radii, not a
 * bounding rectangle. S and E are rays from C. The driver samples at five
 * degrees using Q14 tables, intersects those segments with the rays, then
 * closes through C (pie) or directly (chord). No framebuffer is consulted.
 * Copyright (c) 2026 SimVU. MIT license; see LICENSE. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <limits.h>

#define RIP_AFFINE_MAX_POINTS 76
static const int16_t rip_affine_cos[72] = {
    16384,16321,16135,15825,15395,14848,14188,13420,12550,11585,10531,9397,
    8192,6924,5603,4240,2845,1427,0,-1427,-2845,-4240,-5603,-6924,
    -8191,-9397,-10531,-11585,-12550,-13420,-14188,-14848,-15395,-15825,-16135,-16321,
    -16384,-16321,-16135,-15825,-15395,-14848,-14188,-13420,-12550,-11585,-10531,-9397,
    -8192,-6924,-5603,-4240,-2845,-1427,0,1427,2845,4240,5603,6924,
    8192,9397,10531,11585,12550,13420,14188,14848,15395,15825,16135,16321
};
static const int16_t rip_affine_sin[72] = {
    0,1427,2845,4240,5603,6924,8191,9397,10531,11585,12550,13420,
    14188,14848,15395,15825,16135,16321,16384,16321,16135,15825,15395,14848,
    14188,13420,12550,11585,10531,9397,8191,6924,5603,4240,2845,1427,
    0,-1427,-2845,-4240,-5603,-6924,-8191,-9397,-10531,-11585,-12550,-13420,
    -14188,-14848,-15395,-15825,-16135,-16321,-16384,-16321,-16135,-15825,-15395,-14848,
    -14188,-13420,-12550,-11585,-10531,-9397,-8192,-6924,-5603,-4240,-2845,-1427
};

/* Explicit floor division avoids relying on signed right-shift semantics. */
static int32_t rip_affine_q14(int32_t v) {
    return v >= 0 ? v / 16384 : -((-v + 16383) / 16384);
}

static int rip_affine_angle(int x, int y) {
    double a;
    if (!x) return y < 0 ? 90 : 270;
    a = atan((double)-y / x) * 180.0 / 3.14159265358979323846;
    return (int)a + (x < 0 ? 180 : a < 0 ? 360 : 0);
}

/* First segment intersected by a ray, with the driver's truncation toward
 * zero before inclusive segment/ray bounds checks (RVA 0x00F5A0). */
static int rip_affine_intersect(const int16_t *p, int cx, int cy,
                                int rx, int ry, int16_t *hit) {
    int i;
    for (i = 0; i < 72; ++i) {
        int j = (i + 1) % 72;
        int x = p[2*i], y = p[2*i+1];
        int dx = p[2*j]-x, dy = p[2*j+1]-y;
        double det = (double)dx*ry - (double)dy*rx;
        double t, fx, fy;
        int hx, hy;
        if (det == 0) continue;
        t = ((double)(cx-x)*ry - (double)(cy-y)*rx) / det;
        fx = x + t*dx;
        fy = y + t*dy;
        /* Nearly parallel lines can intersect far beyond the integer range.
         * Such a point cannot belong to this int16 segment. Reject it before
         * the conversion, which would otherwise be undefined in C. */
        if (!isfinite(fx) || !isfinite(fy) ||
            fx < INT_MIN || fx > INT_MAX || fy < INT_MIN || fy > INT_MAX)
            continue;
        hx = (int)fx;
        hy = (int)fy;
        if (hx < (dx < 0 ? x+dx : x) || hx > (dx < 0 ? x : x+dx) ||
            hy < (dy < 0 ? y+dy : y) || hy > (dy < 0 ? y : y+dy) ||
            (rx >= 0 ? hx < cx : hx > cx) ||
            (ry >= 0 ? hy < cy : hy > cy)) continue;
        hit[0] = (int16_t)hx; hit[1] = (int16_t)hy;
        return i;
    }
    return -1;
}

/* mode 0=whole ellipse, 1=arc, 2=pie, 3=chord. xy has five x,y pairs.
 * out has room for RIP_AFFINE_MAX_POINTS pairs. Return point count. */
static int rip_affine_oval_points(const int16_t *xy, int mode,
                                  int16_t *out, bool *closed) {
    int16_t ring[144], start[2], end[2];
    int cx=xy[0], cy=xy[1], ax=xy[2]-cx, ay=xy[3]-cy;
    int bx=xy[4]-cx, by=xy[5]-cy, i, first, last, n=0;
    int sx=xy[6]-cx, sy=xy[7]-cy, ex=xy[8]-cx, ey=xy[9]-cy;
    *closed = true;
    if (ax == 0 && ay == 0 && bx == 0 && by == 0) {
        out[0]=(int16_t)cx; out[1]=(int16_t)cy; *closed=false; return 1;
    }
    if (rip_affine_angle(bx,by) < rip_affine_angle(ax,ay)) {
        i=ax; ax=bx; bx=i; i=ay; ay=by; by=i;
    }
    if (sx == 0 && sy == 0) { sx=ax; sy=ay; }
    if (ex == 0 && ey == 0) { ex=bx; ey=by; }
    for (i=0; i<72; ++i) {
        ring[2*i]=(int16_t)(cx+rip_affine_q14(ax*rip_affine_cos[i]+bx*rip_affine_sin[i]));
        ring[2*i+1]=(int16_t)(cy+rip_affine_q14(ay*rip_affine_cos[i]+by*rip_affine_sin[i]));
    }
    first = rip_affine_intersect(ring,cx,cy,sx,sy,start);
    last = rip_affine_intersect(ring,cx,cy,ex,ey,end);
    /* The driver's failed-ray fallbacks are asymmetric (0x00FDEE).
     * In particular a missing end ray resets FIRST to 71, leaving last=-1. */
    if (first < 0) { first=0; start[0]=ring[0]; start[1]=ring[1]; }
    if (last < 0) { first=71; end[0]=ring[142]; end[1]=ring[143]; }
    if (!mode ||
        (start[0] == end[0] && start[1] == end[1])) {
        for (i=0;i<144;++i) out[i]=ring[i];
        return 72;
    }
    out[0]=start[0]; out[1]=start[1]; n=1;
    /* Same-segment direction test (0x00F515) uses X, or Y for a vertical
     * segment. Distance ordering is wrong after integer intersections. */
    i=(first+1)%72;
    if (last >= 0 && (first != last ||
        (ring[2*i] != ring[2*first] ?
         ((ring[2*i] > ring[2*first] && end[0] < start[0]) ||
          (ring[2*i] < ring[2*first] && end[0] > start[0])) :
         ((ring[2*i+1] > ring[2*first+1] && end[1] < start[1]) ||
          (ring[2*i+1] < ring[2*first+1] && end[1] > start[1]))))) {
        for (i=(first+1)%72;;i=(i+1)%72) {
            out[2*n]=ring[2*i]; out[2*n+1]=ring[2*i+1]; ++n;
            if (i == last) break;
        }
    }
    out[2*n]=end[0]; out[2*n+1]=end[1]; ++n;
    if (mode == 2) { out[2*n]=(int16_t)cx; out[2*n+1]=(int16_t)cy; ++n; }
    if (mode != 1) { out[2*n]=out[0]; out[2*n+1]=out[1]; ++n; }
    else *closed=false;
    return n;
}
