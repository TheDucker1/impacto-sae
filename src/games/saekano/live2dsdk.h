#pragma once

#include "formats.h"
#include "../../renderer/opengl/cubism2.h"

// Retain the existing source API for external consumers of the Saekano library.
namespace Impacto::Saekano {
using Cubism2Context = OpenGL::Cubism2Context;
using Cubism2Sdk = OpenGL::Cubism2Sdk;
using Cubism2Model = OpenGL::Cubism2Model;
}  // namespace Impacto::Saekano
