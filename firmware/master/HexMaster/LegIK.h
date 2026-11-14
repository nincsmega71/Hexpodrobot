/*
 * Egy 3 szabadságfokú láb inverz kinematikája
 *
 * Láb-koordinátarendszer (a csípőtengelyben):
 *   x: kifelé a testből, y: előre, z: felfelé (a talppont z-je negatív)
 * Nullállás: csípő egyenesen kifelé, comb vízszintes, lábszár a comb folytatása.
 * Szögek 0,1 fokban, pozitív = az óramutatóval ellentétes a tengely felől nézve.
 * A tényleges szervóirányt és a nullpontot a szervó DIRECTION és POT_ZERO
 * regiszterével kell egyeztetni (lásd a "nulla" parancsot a HexMaster-ben).
 */
#pragma once
#include <math.h>
#include <stdint.h>

struct LegGeom {
  float coxa  = 35.0f;   // mm, csípőtengely -> combtengely
  float femur = 60.0f;   // mm
  float tibia = 90.0f;   // mm
};

struct LegAngles {
  int16_t coxa_x10, femur_x10, tibia_x10;
};

// true, ha a pont elérhető
inline bool legIK(const LegGeom &g, float x, float y, float z, LegAngles &out) {
  const float rad2x10 = 1800.0f / (float)M_PI;
  float coxa = atan2f(y, x);
  float r = sqrtf(x * x + y * y) - g.coxa;       // vízszintes távolság a combtengelytől
  float d2 = r * r + z * z;
  float d = sqrtf(d2);
  if (d > g.femur + g.tibia - 0.5f || d < fabsf(g.femur - g.tibia) + 0.5f) return false;

  float a1 = atan2f(z, r);                                         // a talppont iránya
  float a2 = acosf((g.femur * g.femur + d2 - g.tibia * g.tibia) / (2 * g.femur * d));
  float femur = a1 + a2;                                           // comb felfelé hajlik
  float knee  = acosf((g.femur * g.femur + g.tibia * g.tibia - d2) / (2 * g.femur * g.tibia));
  float tibia = knee - (float)M_PI;                                // 0 = egyenes láb

  out.coxa_x10  = (int16_t)lroundf(coxa  * rad2x10);
  out.femur_x10 = (int16_t)lroundf(femur * rad2x10);
  out.tibia_x10 = (int16_t)lroundf(tibia * rad2x10);
  return true;
}
