#pragma once

#include "../cubism2.h"

// Compatibility for existing consumers; use the renderer-neutral header.
namespace Impacto::OpenGL {
using Cubism2Context = Impacto::Cubism2Context;
using Cubism2Sdk = Impacto::Cubism2Sdk;
using Cubism2Model = Impacto::Cubism2Model;
}
