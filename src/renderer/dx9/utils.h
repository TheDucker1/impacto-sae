#pragma once

#include <d3d9.h>
#include <ankerl/unordered_dense.h>
#include <cstdint>
#include <string>
#include "../../formaterror.h"

namespace Impacto {
namespace DirectX9 {

inline void Check(HRESULT result, const char* operation) {
  if (FAILED(result))
    throw FormatError(std::string(operation) + " failed (HRESULT " +
                      std::to_string(static_cast<uint32_t>(result)) + ")");
}

inline ankerl::unordered_dense::map<uint32_t, IDirect3DTexture9*> Textures;

}  // namespace DirectX9
}  // namespace Impacto
