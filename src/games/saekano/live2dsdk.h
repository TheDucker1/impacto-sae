#pragma once

#include "formats.h"
#include "../../renderer/cubism2.h"

// Retain the existing source API for external consumers of the Saekano library.
namespace Impacto::Saekano {
using Cubism2Context = Impacto::Cubism2Context;
using Cubism2Sdk = Impacto::Cubism2Sdk;
using Cubism2Model = Impacto::Cubism2Model;
}  // namespace Impacto::Saekano
