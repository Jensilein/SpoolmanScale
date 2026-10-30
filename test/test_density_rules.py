"""Compile the production density rules and check which densities are
plausible, when a database entry is the product named on a Bambu tag (the
vendor, the product name with or without a colour word after it, the net
weight where both name one), which material a support code is listed
under, and which typical density a material is offered
(services/density_rules.cpp)."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/density_rules.h"
#include <cassert>
#include <cmath>
#include <cstring>

int main() {
  assert(densityPlausible(1.16f));
  assert(densityPlausible(0.50f) && densityPlausible(4.00f));
  assert(!densityPlausible(0.49f) && !densityPlausible(4.01f));
  assert(!densityPlausible(0.0f) && !densityPlausible(-1.2f) && !densityPlausible(NAN));
  assert(!densityPlausible(11.6f));

  // The names as SpoolmanDB carries them (Spoolman 0.27.0, 30.10.2026) and
  // as BambuStudio's catalogue names the products.
  const DensityProduct abs_s = { "Bambu Lab", "Support for ABS", 500 };
  assert(densityEntryIsProduct("Bambu Lab", "Support for ABS", 500, abs_s));
  assert(densityEntryIsProduct(" bambu lab ", "support FOR abs ", 500, abs_s));
  assert(densityEntryIsProduct("Bambu Lab", "Support for ABS", 0, abs_s));
  assert(!densityEntryIsProduct("Bambu Lab", "Support for ABS", 1000, abs_s));
  assert(!densityEntryIsProduct("Sunlu", "Support for ABS", 500, abs_s));
  assert(!densityEntryIsProduct("Bambu Lab", "Support for ABS-GF", 500, abs_s));
  assert(!densityEntryIsProduct("Bambu Lab", "ABS", 500, abs_s));

  const DensityProduct pa_s = { "Bambu Lab", "Support for PA/PET", 500 };
  assert(densityEntryIsProduct("Bambu Lab", "Support for PA/PET", 500, pa_s));

  // "Support for PLA" finds the white one (1.33), not the PLA/PETG ones (1.28).
  const DensityProduct pla_s = { "Bambu Lab", "Support for PLA", 500 };
  assert(densityEntryIsProduct("Bambu Lab", "Support for PLA White", 500, pla_s));
  assert(!densityEntryIsProduct("Bambu Lab", "Support for PLA/PETG Black", 500, pla_s));
  const DensityProduct plapetg = { "Bambu Lab", "Support for PLA/PETG", 500 };
  assert(densityEntryIsProduct("Bambu Lab", "Support for PLA/PETG Nature", 500, plapetg));
  assert(!densityEntryIsProduct("Bambu Lab", "Support for PLA White", 500, plapetg));

  const DensityProduct none = { "Bambu Lab", "", 500 };
  assert(!densityEntryIsProduct("Bambu Lab", "", 500, none));
  assert(!densityEntryIsProduct(nullptr, nullptr, 500, abs_s));

  // The light form lists by material: the base of each support code.
  assert(strcmp(densitySupportBase("ABS-S"), "ABS") == 0);
  assert(strcmp(densitySupportBase("pa-s"), "PA") == 0);
  assert(strcmp(densitySupportBase("PLA-S"), "PLA") == 0);
  assert(densitySupportBase("PLA") == nullptr);
  assert(densitySupportBase("PETG-S") == nullptr);
  assert(densitySupportBase("PLA-SX") == nullptr);
  assert(densitySupportBase("") == nullptr);
  assert(densitySupportBase(nullptr) == nullptr);
  // Typical values: exact names only, a support code is not its base.
  assert(fabsf(densityTypical("PLA") - 1.24f) < 0.001f);
  assert(fabsf(densityTypical(" petg ") - 1.27f) < 0.001f);
  assert(fabsf(densityTypical("tpu-95a") - 1.22f) < 0.001f);
  assert(densityTypical("PLA-S") == 0.0f);
  assert(densityTypical("PAHT-CF") == 0.0f);
  assert(densityTypical("") == 0.0f && densityTypical(nullptr) == 0.0f);
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "density_rules.cpp"), "-o", str(binary)],
                   check=True)
    subprocess.run([str(binary)], check=True)
print("density rules: ok")
