#pragma once

#include "RegionMap.h"

#ifdef MESHCORE_CZ_REGION_PRESET

static inline void bootstrapCzRegions(RegionMap& map) {
  RegionEntry* cz = map.putRegion("cz", 0);
  if (!cz) return;
  cz->flags = 0;

  static const char* counties[] = {
    "cz-pha", "cz-stc", "cz-jhc", "cz-plz", "cz-kvk", "cz-ulk", "cz-lbk",
    "cz-hkk", "cz-pak", "cz-vys", "cz-jmk", "cz-olk", "cz-zlk", "cz-msk",
  };
  for (unsigned i = 0; i < sizeof(counties) / sizeof(counties[0]); i++) {
    RegionEntry* r = map.putRegion(counties[i], cz->id);
    if (r) r->flags = 0;
  }
  map.setDefaultRegion(cz);
}

#endif
