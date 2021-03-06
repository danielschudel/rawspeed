/*
    RawSpeed - RAW file decoder.

    Copyright (C) 2017 Robert Bieber

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#pragma once

#include "common/Common.h" // for ushort16
#include "common/Point.h"  // for iPoint2D
#include <algorithm>       // for adjacent_find
#include <cassert>         // for assert
#include <vector>          // for vector

namespace rawspeed {

// This is a Natural Cubic Spline. The second derivative at curve ends are zero.
// See https://en.wikipedia.org/wiki/Spline_(mathematics)
// section "Algorithm for computing natural cubic splines"
class Spline final {
public:
  static std::vector<ushort16>
  calculateCurve(const std::vector<iPoint2D>& control_points) {
    assert(control_points.size() >= 2 &&
           "Need at least two points to interpolate between");

    // Expect the X coords of the curve to start/end at the extreme values
    assert(control_points.front().x == 0);
    assert(control_points.back().x == 65535);

    assert(std::adjacent_find(
               control_points.cbegin(), control_points.cend(),
               [](const iPoint2D& lhs, const iPoint2D& rhs) -> bool {
                 return lhs.x >= rhs.x;
               }) == control_points.cend() &&
           "The X coordinates must all be strictly increasing");

#ifndef NDEBUG
    // The Y coords must be limited to ushort16
    std::for_each(control_points.cbegin(), control_points.cend(),
                  [](const iPoint2D& p) -> void {
                    assert(p.y >= 0);
                    assert(p.y <= 65535);
                  });
#endif

    const int num_coords = control_points.size();
    const int num_segments = num_coords - 1;

    // These are the constant factors for each segment of the curve.
    // Each segment i will have the formula:
    // f(x) = a[i] + b[i]*(x - x[i]) + c[i]*(x - x[i])^2 + d[i]*(x - x[i])^3
    std::vector<double> a(num_coords);
    std::vector<double> b(num_segments);
    std::vector<double> c(num_coords);
    std::vector<double> d(num_segments);

    // Extra values used during computation
    std::vector<double> h(num_segments);
    std::vector<double> alpha(num_segments);
    std::vector<double> l(num_coords);
    std::vector<double> mu(num_coords);
    std::vector<double> z(num_coords);

    for (int i = 0; i < num_coords; i++)
      a[i] = control_points[i].y;

    for (int i = 0; i < num_segments; i++)
      h[i] = control_points[i + 1].x - control_points[i].x;

    for (int i = 1; i < num_segments; i++)
      alpha[i] =
          (3. / h[i]) * (a[i + 1] - a[i]) - (3. / h[i - 1]) * (a[i] - a[i - 1]);

    l[0] = mu[0] = z[0] = 0;

    for (int i = 1; i < num_segments; i++) {
      l[i] = 2 * (control_points[i + 1].x - control_points[i - 1].x) -
             (h[i - 1] * mu[i - 1]);
      mu[i] = h[i] / l[i];
      z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
    }

    l[num_segments] = 1;
    z[num_segments] = c[num_segments] = 0;

    for (int i = num_segments - 1; i >= 0; i--) {
      c[i] = z[i] - mu[i] * c[i + 1];
      b[i] = (a[i + 1] - a[i]) / h[i] - h[i] * (c[i + 1] + 2 * c[i]) / 3.;
      d[i] = (c[i + 1] - c[i]) / (3. * h[i]);
    }

    std::vector<ushort16> curve(65536);

    for (int i = 0; i < num_segments; i++) {
      for (int x = control_points[i].x; x <= control_points[i + 1].x; x++) {
        double diff = x - control_points[i].x;
        double diff_2 = diff * diff;
        double diff_3 = diff * diff * diff;

        curve[x] = a[i] + b[i] * diff + c[i] * diff_2 + d[i] * diff_3;
      }
    }

    return curve;
  }
};

} // namespace rawspeed
