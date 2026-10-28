// Checks the fuel calibration in firmware/EngineLink/fuelcal.h on a PC:
//   g++ -O2 -std=c++17 -I firmware/EngineLink tools/test_fuelcal.cpp -o /tmp/test_fuelcal && /tmp/test_fuelcal
#include <cstdio>
#include <cstdlib>
#include <random>
#include "fuelcal.h"

using namespace fuelcal;
static int failures = 0;
static void check(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}

int main() {
  // 1. The owner's two fill-ups (2026-10-28): EngineLink counted 13.9 L (no
  //    band split yet), the pump 12.1 L, both topped up to the filler pipe.
  {
    Row r{{13.9, 0, 0, 0}, 12.1, rowVariance(2, 2, 12.1, 13.9)};
    bool use[1] = {true};
    Fit f = solve(&r, use, 1);
    const double sig = sqrt(f.cov[0][0]);
    printf("     one topped-up fill-up: f=%.3f +/- %.3f (%.1f %%)\n", f.th[0], sig, sig / f.th[0] * 100);
    check(f.ok && fabs(f.th[0] - 12.1 / 13.9) < 0.01, "f close to 12.1/13.9");
    check(f.th[1] == 0 && f.th[2] == 0 && f.th[3] == 0, "no band correction from a fill-up without bands");
    Row c{{13.9, 0, 0, 0}, 12.1, rowVariance(1, 1, 12.1, 13.9)};
    Fit g = solve(&c, use, 1);
    printf("     same, nozzle clicked off: f=%.3f +/- %.3f\n", g.th[0], sqrt(g.cov[0][0]));
    check(g.th[0] > f.th[0] + 0.01, "a clicked-off fill-up moves the factor less");
  }
  // 2. Simulated season: true common factor 0.87, idle reads 8 % high on top,
  //    cruise 3 % low. Mixed trips, mixed fill-up quality, realistic noise.
  {
    std::mt19937 rng(7);
    std::normal_distribution<double> nz(0, 1);
    std::uniform_real_distribution<double> u(0, 1);
    const double F = 0.87, G[3] = {0.08 * 0.87, 0.0, -0.03 * 0.87};
    Row rows[30];
    bool use[30];
    int n = 0;
    int qPrev = 2;
    for (int k = 0; k < 12; k++) {
      const double kind = u(rng);  // harbour days, cruises, mixed
      double x[3];
      if (kind < 0.3) { x[0] = 6 + 6 * u(rng); x[1] = 4 + 6 * u(rng); x[2] = 0; }
      else if (kind < 0.7) { x[0] = 2 + 2 * u(rng); x[1] = 10 + 10 * u(rng); x[2] = 40 + 80 * u(rng); }
      else { x[0] = 3 + 3 * u(rng); x[1] = 15 + 15 * u(rng); x[2] = 15 + 30 * u(rng); }
      const double X = x[0] + x[1] + x[2];
      double truth = F * X;
      for (int b = 0; b < 3; b++) truth += G[b] * x[b];
      const int q = u(rng) < 0.4 ? 2 : 1;
      const double y = truth * (1 + 0.015 * nz(rng)) + fillError(q) * nz(rng) + fillError(qPrev) * nz(rng);
      rows[n] = Row{{X, x[0], x[1], x[2]}, y, rowVariance(q, qPrev, y, X)};
      use[n] = true;
      n++;
      qPrev = q;
    }
    Fit f = solve(rows, use, n);
    printf("     12 fill-ups: f=%.3f idle %+.1f %% part %+.1f %% cruise %+.1f %% (f +/- %.1f %%)\n", f.th[0],
           f.th[1] / f.th[0] * 100, f.th[2] / f.th[0] * 100, f.th[3] / f.th[0] * 100,
           sqrt(f.cov[0][0]) / f.th[0] * 100);
    // A typical cruise trip: 3 L idle, 15 L part load, 60 L cruise.
    const Row trip{{78, 3, 15, 60}, 0, 0};
    const double est = predict(f, trip), truth = F * 78 + G[0] * 3 + G[2] * 60;
    const double sd = sqrt(predictVar(f, trip));
    printf("     cruise trip: estimated %.2f L, truth %.2f L, error %+.2f %% (+/- %.1f %%)\n", est, truth,
           (est - truth) / truth * 100, sd / est * 100);
    check(fabs(est - truth) / truth < 0.02, "cruise trip within 2 %");
    check(fabs(f.th[0] - F) < 0.04, "common factor recovered");
    // An odd fill-up (tank not really full): 25 % short.
    rows[n] = rows[3];
    rows[n].y *= 0.75;
    use[n] = true;
    const double z = zScore(rows, use, n + 1, n);
    printf("     fill-up 25 %% short: z = %.1f\n", z);
    check(fabs(z) > 3, "odd fill-up is caught");
    const double zGood = zScore(rows, use, n, 3);
    printf("     normal fill-up: z = %.1f\n", zGood);
    check(fabs(zGood) < 3, "normal fill-up passes");
  }
  printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
