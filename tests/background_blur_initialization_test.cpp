#define NOMINMAX
#include "Menu/BackgroundBlur.h"
#include "Utils/DeviceContextState.h"
#include "Utils/ShaderCompiler.h"
#include <d3d10_1.h>
#include <d3dcompiler.h>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace globals
{
	namespace game
	{
		bool isVR = false;
	}
	namespace d3d
	{
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
	}
}
namespace
{
	unsigned compileCalls = 0;
	unsigned resourceNames = 0;
	unsigned warnings = 0;
	unsigned errors = 0;
	bool failCompilation = false;

	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
}
namespace logger
{
	template <class... T>
	void warn(const char*, T&&...)
	{
		++warnings;
	}
	template <class... T>
	void error(const char*, T&&...)
	{
		++errors;
	}
}
namespace Util
{
	void SetResourceName(ID3D11DeviceChild* resource, const char*, ...)
	{
		Require(resource != nullptr, "only valid resources are named");
		++resourceNames;
	}

	ID3D11DeviceChild* CompileShader(const wchar_t* path,
		const std::vector<std::pair<const char*, const char*>>&,
		const char* profile, const char* entryPoint, ShaderCompileTiming*)
	{
		++compileCalls;
		if (failCompilation)
			return nullptr;
		const auto source = std::filesystem::path(CSX_BLUR_SHADER_DIR) / std::filesystem::path(path).filename();
		winrt::com_ptr<ID3DBlob> bytecode;
		winrt::com_ptr<ID3DBlob> diagnostics;
		const auto result = D3DCompileFromFile(source.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
			entryPoint, profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, bytecode.put(), diagnostics.put());
		if (FAILED(result) && diagnostics)
			std::cerr << static_cast<const char*>(diagnostics->GetBufferPointer());
		winrt::check_hresult(result);
		if (std::string_view(profile) == "vs_5_0") {
			winrt::com_ptr<ID3D11VertexShader> shader;
			winrt::check_hresult(globals::d3d::device->CreateVertexShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.put()));
			return shader.detach();
		}
		Require(std::string_view(profile) == "ps_5_0", "expected a blur pixel shader");
		winrt::com_ptr<ID3D11PixelShader> shader;
		winrt::check_hresult(globals::d3d::device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.put()));
		return shader.detach();
	}
}
namespace BackgroundBlur
{
#include "ui_blur_initialization.h"
#include "ui_blur_lifecycle.h"
}

int main()
{
	try {
		globals::game::isVR = true;
		Require(BackgroundBlur::Initialize(), "unsupported runtime is a successful no-op without a device");
		Require(!BackgroundBlur::IsEnabled() && compileCalls == 0 && resourceNames == 0,
			"VR initialization does not allocate resources or enable rendering");

		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
			nullptr, 0, D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
		globals::d3d::device = device.get();
		globals::d3d::context = context.get();
		auto d3d10Supported = [&] {
			winrt::com_ptr<ID3D10Device> device10;
			return SUCCEEDED(device->QueryInterface(__uuidof(ID3D10Device), device10.put_void()));
		};
		const auto initialD3D10Support = d3d10Supported();
		for (bool requested : { false, true }) {
			BackgroundBlur::SetEnabled(requested);
			Require(BackgroundBlur::Initialize() && BackgroundBlur::Initialize(), "repeated VR initialization succeeds");
			Require(BackgroundBlur::GetEnabled() == requested && !BackgroundBlur::IsEnabled(),
				"VR retains the saved preference without enabling blur");
			Require(d3d10Supported() == initialD3D10Support && compileCalls == 0 && resourceNames == 0,
				"direct VR initialization preserves device API detection");
			BackgroundBlur::Cleanup();
		}
		Require(warnings == 0 && errors == 0, "unsupported VR is not reported as an initialization error");

		globals::game::isVR = false;
		for (int cycle = 0; cycle < 2; ++cycle) {
			BackgroundBlur::SetEnabled(true);
			const auto previousCompiles = compileCalls;
			Require(BackgroundBlur::Initialize() && BackgroundBlur::IsEnabled(), "flat blur initializes after cleanup");
			Require(compileCalls == previousCompiles + 3 && BackgroundBlur::isolatedState &&
						BackgroundBlur::vertexShader && BackgroundBlur::horizontalPixelShader &&
						BackgroundBlur::verticalPixelShader && BackgroundBlur::constantBuffer &&
						BackgroundBlur::samplerState && BackgroundBlur::blendState && BackgroundBlur::scissorRasterizerState,
				"flat initialization creates all shaders and fixed resources");
			Require(BackgroundBlur::Initialize() && compileCalls == previousCompiles + 3,
				"repeated flat initialization reuses resources");
			BackgroundBlur::Cleanup();
			Require(!BackgroundBlur::IsEnabled() && !BackgroundBlur::isolatedState && !BackgroundBlur::isolatedContext,
				"cleanup releases context ownership and disables rendering");
		}
		failCompilation = true;
		Require(!BackgroundBlur::Initialize() && !BackgroundBlur::IsEnabled(), "flat shader failure remains a failure");
		const auto failedCompiles = compileCalls;
		Require(!BackgroundBlur::Initialize() && compileCalls == failedCompiles, "failed initialization is not retried every frame");
		BackgroundBlur::Cleanup();
		failCompilation = false;
		Require(BackgroundBlur::Initialize(), "explicit cleanup permits recovery after failure");
		BackgroundBlur::Cleanup();
		std::cout << "VR API preservation and flat blur lifecycle passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
