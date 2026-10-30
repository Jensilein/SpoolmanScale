#pragma once

#include <stddef.h>

// ============================================================
//  WHICH DENSITY A FILAMENT GETS: THE RULES
//
//  Spoolman needs a density to create a filament. A filament from a tag asks
//  the filament database by vendor and material first, then Spoolman's
//  material list (spoolman_filament.cpp). A Bambu support tag finds neither:
//  it names its material ABS-S, PA-S or PLA-S, and the database lists those
//  products under the base material and the product name, "Support for ABS"
//  as ABS at 1.16 g/cm3. So the product name has to find the density, and
//  only the density: the filament is still created from the tag, with the
//  tag's material, which the AMS and the slicer read.
//
//  The rules live here, without Arduino, the backend or the screens, so that
//  test/test_density_rules.py can hold them.
//
//    plausible   A density is a number from DENSITY_MIN_G_CM3 (0.50) to
//                DENSITY_MAX_G_CM3 (4.00) g/cm3, both ends in. Anything else
//                is no density: not a number, zero, a negative, and a value
//                far above the range (11.6 for 1.16).
//
//    product     A database entry is the product of the tag when its vendor
//                is the same, its name is the product name or the product
//                name, a blank and more text (a colour word, as in
//                "Support for PLA White"), and its net weight is the same
//                where both name one. A longer word is another product:
//                "Support for PLA/PETG Black" is not "Support for PLA".
//
//    support     The light form of the database has no word search, only
//                the maker's list of one material. For the three support
//                codes the base material to list is known; any other
//                material is listed as it is and needs no help.
// ============================================================

#define DENSITY_MIN_G_CM3 0.50f
#define DENSITY_MAX_G_CM3 4.00f

// A product as a tag names it: the vendor, the product name and the net
// weight in grams (0 when the tag names none).
struct DensityProduct {
  const char* vendor;
  const char* product;
  int net_weight_g;
};

// True when d is a plausible density, see above.
bool densityPlausible(float d);

// True when the database entry is the product, see above. Vendor and name
// are compared without case (ASCII only) and with the blanks at both ends
// ignored. A null vendor or name, and an empty product name, never match.
bool densityEntryIsProduct(const char* entry_vendor, const char* entry_name,
                           int entry_weight_g, const DensityProduct& want);

// The material the database lists a Bambu support code under ("ABS-S" ->
// "ABS"), in any case of letters; nullptr for every other material.
const char* densitySupportBase(const char* tag_material);
