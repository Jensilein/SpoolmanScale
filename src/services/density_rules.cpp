#include "services/density_rules.h"

#include <string.h>

// ASCII letters only: a name with an umlaut is compared byte for byte, so no
// locale can fold half of a UTF-8 sequence.
static char foldAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

// Where the text of s begins and how many bytes it has without the blanks on
// both sides. Only the space counts as a blank.
static size_t trimmedSpan(const char* s, const char** begin) {
  size_t skip = 0;
  while (s[skip] == ' ') skip++;
  size_t end = strlen(s);
  while (end > skip && s[end - 1] == ' ') end--;
  *begin = s + skip;
  return end - skip;
}

// True when the first len bytes of a and b are the same, ASCII case aside.
static bool sameFolded(const char* a, const char* b, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (foldAscii(a[i]) != foldAscii(b[i])) return false;
  }
  return true;
}

bool densityPlausible(float d) {
  // Both comparisons are false for a value that is not a number.
  return d >= DENSITY_MIN_G_CM3 && d <= DENSITY_MAX_G_CM3;
}

bool densityEntryIsProduct(const char* entry_vendor, const char* entry_name,
                           int entry_weight_g, const DensityProduct& want) {
  if (!entry_vendor || !entry_name || !want.vendor || !want.product) return false;

  const char* vendor;
  const size_t vlen = trimmedSpan(entry_vendor, &vendor);
  const char* want_vendor;
  const size_t wvlen = trimmedSpan(want.vendor, &want_vendor);
  if (vlen != wvlen || !sameFolded(vendor, want_vendor, vlen)) return false;

  // An empty product name names no product, so it matches nothing.
  const char* product;
  const size_t plen = trimmedSpan(want.product, &product);
  if (plen == 0) return false;

  // The product name, or the product name, a blank and more text.
  const char* name;
  const size_t nlen = trimmedSpan(entry_name, &name);
  if (nlen < plen || !sameFolded(name, product, plen)) return false;
  if (nlen > plen && name[plen] != ' ') return false;

  // The net weight counts only where both name one.
  if (entry_weight_g > 0 && want.net_weight_g > 0 && entry_weight_g != want.net_weight_g) return false;
  return true;
}

// Bambu's support codes as block 2 of the tag carries them, and the material
// SpoolmanDB lists the product under (checked against SpoolmanDB on
// 30.10.2026: "Support for ABS" ABS, "Support for PA/PET" PA, "Support for
// PLA White" and "Support for PLA/PETG Black/Nature" PLA).
static const struct { const char* code; const char* base; } SUPPORT_BASES[] = {
  { "abs-s", "ABS" },
  { "pa-s",  "PA"  },
  { "pla-s", "PLA" },
};

const char* densitySupportBase(const char* tag_material) {
  if (!tag_material) return nullptr;
  const size_t len = strlen(tag_material);
  for (const auto& s : SUPPORT_BASES) {
    if (len == strlen(s.code) && sameFolded(tag_material, s.code, len)) return s.base;
  }
  return nullptr;
}

// The median density of each material across SpoolmanDB, every maker
// together (9 457 filaments on Spoolman 0.27.0, 30.10.2026). Only materials
// with enough entries to have a typical value, and only the exact name: a
// support or a filled variant is a different material ("PLA-S" is 1.33,
// "PLA" 1.24), so nothing is matched by its prefix.
static const struct { const char* material; float density; } TYPICAL[] = {
  { "PLA",     1.24f }, { "PLA+",    1.24f }, { "PLA-CF",  1.24f },
  { "PETG",    1.27f }, { "PETG-CF", 1.26f }, { "PETG-GF", 1.27f },
  { "PCTG",    1.24f }, { "PET-CF",  1.30f },
  { "ABS",     1.06f }, { "ABS+",    1.04f }, { "ABS-GF",  1.08f },
  { "ASA",     1.07f }, { "ASA-CF",  1.11f },
  { "TPU",     1.20f }, { "TPU-95A", 1.22f }, { "TPU-90A", 1.23f }, { "TPE", 1.24f },
  { "PC",      1.20f }, { "PC-CF",   1.36f },
  { "PA",      1.12f }, { "PA6",     1.22f }, { "PA12",    1.10f }, { "PA6-CF", 1.12f },
  { "HIPS",    1.04f }, { "PVA",     1.22f }, { "PVB",     1.10f }, { "PHA",    1.21f },
};

float densityTypical(const char* material) {
  if (!material) return 0.0f;
  const char* m;
  const size_t len = trimmedSpan(material, &m);
  for (const auto& t : TYPICAL) {
    if (len == strlen(t.material) && sameFolded(m, t.material, len)) return t.density;
  }
  return 0.0f;
}
