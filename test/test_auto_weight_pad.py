"""Compile the production rule that keeps automatic weighing from filing a
spool whose tag lies on the reader without the spool on the pad, and read it
back (services/auto_weight_state.cpp)."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r"""
#include "services/auto_weight_state.h"
#include <cassert>

int main() {
  // A tag alone over an empty pad: too light, own empty weight or not.
  assert(autoWeightPadTooLight(0.0f, 180.0f, true));
  assert(autoWeightPadTooLight(3.0f, 180.0f, false));
  assert(autoWeightPadTooLight(19.0f, 0.0f, false));
  assert(!autoWeightPadTooLight(20.0f, 0.0f, false));

  // Measured for this spool (180 g): an empty spool reads near it and counts,
  // half the spool's weight does not.
  assert(!autoWeightPadTooLight(170.0f, 180.0f, true));
  assert(!autoWeightPadTooLight(165.0f, 180.0f, true));
  assert(autoWeightPadTooLight(164.0f, 180.0f, true));
  assert(autoWeightPadTooLight(90.0f, 180.0f, true));

  // Only a filament's or vendor's default (250 g): a lighter cardboard spool
  // of the same filament, empty at 140 g, still counts.
  assert(!autoWeightPadTooLight(140.0f, 250.0f, false));

  // A light spool: the floor never drops below the empty-pad limit.
  assert(autoWeightPadTooLight(19.0f, 30.0f, true));
  assert(!autoWeightPadTooLight(25.0f, 30.0f, true));
  return 0;
}
"""

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "auto_weight_state.cpp"), "-o", str(binary)],
                   check=True)
    subprocess.run([str(binary)], check=True)
print("auto weight pad rule: ok")
