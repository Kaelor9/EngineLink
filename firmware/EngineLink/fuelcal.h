#pragma once
// Fuel calibration from fill-ups (no Arduino code, so it is tested on a PC:
// tools/test_fuelcal.cpp).
//
// Every pair of full tanks gives one equation:
//
//   pump litres = f * X + g0 * x0 + g1 * x1 + g2 * x2
//
// X is everything EngineLink counted in between (at the base scale, without
// correction), x0..x2 the part counted at idle, part load and cruise. f is
// the common correction, g the extra correction per engine-speed band.
// Each equation is weighted by how exact the fill-ups were, and the unknowns
// have a prior (f near 1, g near 0). With few fill-ups the band corrections
// stay at 0 and only f moves; with fill-ups of different kinds of trips the
// bands separate by themselves. The result comes with its own uncertainty.
#include <math.h>

namespace fuelcal {

constexpr int P = 4;
constexpr double PRIOR_F = 0.30;    // 1 sigma of the common factor before any fill-up
constexpr double PRIOR_BAND = 0.06; // 1 sigma of a band correction

struct Row {
  double a[P];  // X, x0, x1, x2 (litres at the base scale)
  double y;     // pump litres
  double var;   // variance of y (L^2)
};
struct Fit {
  double th[P];      // f, g0, g1, g2
  double cov[P][P];  // covariance of th
  bool ok;
};

// Gauss-Jordan with partial pivoting. Returns false if m is singular.
inline bool invert(const double m[P][P], double out[P][P]) {
  double a[P][2 * P];
  for (int i = 0; i < P; i++)
    for (int j = 0; j < 2 * P; j++) a[i][j] = j < P ? m[i][j] : (j - P == i ? 1.0 : 0.0);
  for (int c = 0; c < P; c++) {
    int piv = c;
    for (int r = c + 1; r < P; r++)
      if (fabs(a[r][c]) > fabs(a[piv][c])) piv = r;
    if (fabs(a[piv][c]) < 1e-12) return false;
    if (piv != c)
      for (int j = 0; j < 2 * P; j++) { double t = a[c][j]; a[c][j] = a[piv][j]; a[piv][j] = t; }
    const double d = a[c][c];
    for (int j = 0; j < 2 * P; j++) a[c][j] /= d;
    for (int r = 0; r < P; r++) {
      if (r == c) continue;
      const double k = a[r][c];
      if (k != 0)
        for (int j = 0; j < 2 * P; j++) a[r][j] -= k * a[c][j];
    }
  }
  for (int i = 0; i < P; i++)
    for (int j = 0; j < P; j++) out[i][j] = a[i][j + P];
  return true;
}

// Fit over the rows with use[i] true; skip is one more row to leave out
// (for checking that row against all the others), -1 for none.
inline Fit solve(const Row *rows, const bool *use, int n, int skip = -1) {
  Fit f{};
  double N[P][P] = {}, b[P] = {};
  const double prior[P] = {1.0 / (PRIOR_F * PRIOR_F), 1.0 / (PRIOR_BAND * PRIOR_BAND),
                           1.0 / (PRIOR_BAND * PRIOR_BAND), 1.0 / (PRIOR_BAND * PRIOR_BAND)};
  for (int i = 0; i < P; i++) N[i][i] = prior[i];
  b[0] = prior[0] * 1.0;  // f is expected near 1, the bands near 0
  for (int k = 0; k < n; k++) {
    if (!use[k] || k == skip) continue;
    const Row &r = rows[k];
    const double w = 1.0 / r.var;
    for (int i = 0; i < P; i++) {
      b[i] += w * r.a[i] * r.y;
      for (int j = 0; j < P; j++) N[i][j] += w * r.a[i] * r.a[j];
    }
  }
  f.ok = invert(N, f.cov);
  if (!f.ok) { f.th[0] = 1; return f; }
  for (int i = 0; i < P; i++) {
    f.th[i] = 0;
    for (int j = 0; j < P; j++) f.th[i] += f.cov[i][j] * b[j];
  }
  return f;
}

inline double predict(const Fit &f, const Row &r) {
  double y = 0;
  for (int i = 0; i < P; i++) y += f.th[i] * r.a[i];
  return y;
}
// Variance of the prediction for this row, from the fit's own uncertainty.
inline double predictVar(const Fit &f, const Row &r) {
  double v = 0;
  for (int i = 0; i < P; i++)
    for (int j = 0; j < P; j++) v += r.a[i] * f.cov[i][j] * r.a[j];
  return v;
}
// How far a row is from what all the other rows predict, in standard
// deviations of that comparison.
inline double zScore(const Row *rows, const bool *use, int n, int k) {
  const Fit f = solve(rows, use, n, k);
  if (!f.ok) return 0;
  const double v = predictVar(f, rows[k]) + rows[k].var;
  return (rows[k].y - predict(f, rows[k])) / sqrt(v);
}
// Uncertainty of a fill-up's pump litres (1 sigma, litres).
inline double fillError(int quality) { return quality == 2 ? 0.3 : 2.0; }
// Variance of one equation: both full tanks, the pump, and how much the
// engine's own estimate scatters from trip to trip.
inline double rowVariance(int qualityEnd, int qualityStart, double pumpLitres, double counted) {
  const double a = fillError(qualityEnd), b = fillError(qualityStart);
  return a * a + b * b + (0.005 * pumpLitres) * (0.005 * pumpLitres) + (0.015 * counted) * (0.015 * counted);
}

}  // namespace fuelcal
