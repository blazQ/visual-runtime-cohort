#pragma once
#include "visual_runtime/api.h"

#include <cstdio>

using VisualRuntimeGetAPIFn = const VRTAPI *(*)();

inline bool visual_runtime_api_valid(const VRTAPI &api) {
  if (api.abi_version != VISUAL_RUNTIME_API_VERSION) {
    std::fprintf(
        stderr,
        "[visual_runtime_api] incompatible ABI version: got %u, expected %u\n",
        api.abi_version, VISUAL_RUNTIME_API_VERSION);
    return false;
  }
  if (api.struct_size < sizeof(VRTAPI)) {
    std::fprintf(stderr,
                 "[visual_runtime_api] incompatible API size: got %u, expected "
                 "at least %zu\n",
                 api.struct_size, sizeof(VRTAPI));
    return false;
  }
  if (!api.backend_name) {
    std::fprintf(stderr,
                 "[visual_runtime_api] missing required backend_name field\n");
    return false;
  }
  if (!api.init || !api.change_view || !api.update) {
    std::fprintf(stderr,
                 "[visual_runtime_api] missing required lifecycle functions\n");
    return false;
  }
  return true;
}
