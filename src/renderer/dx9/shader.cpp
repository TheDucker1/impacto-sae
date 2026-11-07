#include "shader.h"

#include <d3dcompiler.h>

#include "renderer.h"
#include "../../log.h"
#include "utils.h"
#include <wrl/client.h>

namespace Impacto {
namespace DirectX9 {

static char const ShaderPath[] = "./shaders/dx9";
static char const FragShaderExtension[] = "_frag.hlsl";
static char const VertShaderExtension[] = "_vert.hlsl";

Shader::~Shader() {
  if (VertexShader) VertexShader->Release();
  if (PixelShader) PixelShader->Release();
}

void Shader::Compile(char const* name, IDirect3DDevice9* device,
                     IDirect3DVertexDeclaration9* vertexDeclaration,
                     D3D_SHADER_MACRO* macros) {
  ImpLog(LogLevel::Debug, LogChannel::Render, "Compiling shader \"{:s}\"\n",
         name);

  VertexDeclaration = vertexDeclaration;

  const auto compile = [&](const char* extension, const char* target) {
    const auto path = fmt::format("{}/{}{}", ShaderPath, name, extension);
    const std::filesystem::path nativePath(path);
    Microsoft::WRL::ComPtr<ID3DBlob> code, error;
    const auto result = D3DCompileFromFile(
        nativePath.c_str(), macros, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main",
        target, D3DCOMPILE_IEEE_STRICTNESS, 0, &code, &error);
    if (FAILED(result))
      throw FormatError(path + ": " +
                        (error ? std::string(static_cast<const char*>(
                                                 error->GetBufferPointer()),
                                             error->GetBufferSize())
                               : "shader source or compiler unavailable"));
    return code;
  };
  auto vertex = compile(VertShaderExtension, "vs_3_0");
  auto pixel = compile(FragShaderExtension, "ps_3_0");
  Check(device->CreateVertexShader(
            static_cast<DWORD*>(vertex->GetBufferPointer()), &VertexShader),
        "Create vertex shader");
  Check(device->CreatePixelShader(
            static_cast<DWORD*>(pixel->GetBufferPointer()), &PixelShader),
        "Create pixel shader");
}

void Shader::UseShader(IDirect3DDevice9* device) {
  device->SetVertexDeclaration(VertexDeclaration);
  device->SetVertexShader(VertexShader);
  device->SetPixelShader(PixelShader);
}

}  // namespace DirectX9
}  // namespace Impacto
