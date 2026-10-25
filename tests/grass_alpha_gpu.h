struct GrassAlphaFixture
{
	using Pixel = std::array<float, 4>;
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	ComPtr<ID3D11Texture2D> target, staging;
	ComPtr<ID3D11RenderTargetView> rtv;
	ComPtr<ID3D11SamplerState> sampler;
	std::array<ComPtr<ID3D11ShaderResourceView>, 2> textures;

	GrassAlphaFixture()
	{
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
			device.GetAddressOf(), nullptr, context.GetAddressOf()));
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = 4;
		desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		Check(device->CreateTexture2D(&desc, nullptr, target.GetAddressOf()));
		Util::SetResourceName(target.Get(), "GrassAlphaTest::Target");
		Check(device->CreateRenderTargetView(target.Get(), nullptr, rtv.GetAddressOf()));
		Util::SetResourceName(rtv.Get(), "GrassAlphaTest::Target RTV");
		desc.BindFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
		Util::SetResourceName(staging.Get(), "GrassAlphaTest::Readback");
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.CPUAccessFlags = 0;
		// The third texel fails only after fading; the fourth sits exactly on the threshold.
		const std::array<Pixel, 8> texels{ Pixel{ 0.2f, 0.3f, 0.4f, 0.25f }, Pixel{ 0.2f, 0.3f, 0.4f, 0.75f },
			Pixel{ 0.2f, 0.3f, 0.4f, 0.75f }, Pixel{ 0.2f, 0.3f, 0.4f, 0.5f },
			Pixel{ 0.5f, 0.5f, 1, 1 }, Pixel{ 0.5f, 0.5f, 1, 1 }, Pixel{ 0.5f, 0.5f, 1, 1 }, Pixel{ 0.5f, 0.5f, 1, 1 } };
		D3D11_SUBRESOURCE_DATA initial{ texels.data(), 4 * sizeof(Pixel), 0 };
		for (UINT atlas = 0; atlas < textures.size(); ++atlas) {
			desc.Height = atlas + 1;
			ComPtr<ID3D11Texture2D> texture;
			Check(device->CreateTexture2D(&desc, &initial, texture.GetAddressOf()));
			Util::SetResourceName(texture.Get(), "GrassAlphaTest::Texture%u", atlas);
			Check(device->CreateShaderResourceView(texture.Get(), nullptr, textures[atlas].GetAddressOf()));
			Util::SetResourceName(textures[atlas].Get(), "GrassAlphaTest::Texture%u SRV", atlas);
		}
		D3D11_SAMPLER_DESC sample{};
		sample.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		sample.AddressU = sample.AddressV = sample.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sample.MaxLOD = D3D11_FLOAT32_MAX;
		Check(device->CreateSamplerState(&sample, sampler.GetAddressOf()));
		Util::SetResourceName(sampler.Get(), "GrassAlphaTest::Sampler");
	}

	unsigned CheckVariant(bool vr, bool optimized, bool enhanced, bool pbr, bool depth, bool alpha)
	{
		constexpr const char* source = R"(
#include "RunGrass.hlsl"
PS_INPUT AlphaVertex(uint vertex : SV_VertexID) {
    const float2 corners[6] = { float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1) };
    uint texel = vertex / 6;
    float2 corner = corners[vertex % 6];
    float4 position = float4(-1 + (texel + corner.x) * 0.5, 1 - corner.y * 2, 0.5, 1);
    PS_INPUT input = (PS_INPUT)0;
    input.HPosition = position;
    input.WorldPosition = float4(position.xy, 1, 1);
    input.PreviousWorldPosition = input.WorldPosition;
    input.ViewSpacePosition = input.WorldPosition.xyz;
#ifdef GRASS_LIGHTING
    input.VertexNormal = float4(0, 0, 1, 1);
#endif
    input.TexCoord.xy = float2((texel + 0.5) / 4, 0.75);
    input.Color = float4(1, 1, 1, texel == 2 ? 0.5 : 1);
#ifdef RENDER_DEPTH
    input.Depth = float2(0.5, 1);
#endif
#ifdef VR
    input.ClipDistance = input.CullDistance = 1;
#endif
    return input;
}
)";
		auto defines = GrassDefines(false, vr, enhanced, pbr, depth, alpha, false, optimized);
		ComPtr<ID3DBlob> code, vertexCode;
		try {
			code = CompileProgram(defines.data(), false);
			vertexCode = CompileProgram(defines.data(), true, source, "AlphaVertex");
		} catch (...) {
			std::cerr << "Alpha permutation: VR=" << vr << " GO=" << optimized << " enhanced=" << enhanced
					  << " PBR=" << pbr << " depth=" << depth << " alpha=" << alpha << '\n';
			throw;
		}
		ComPtr<ID3D11PixelShader> pixel;
		ComPtr<ID3D11VertexShader> vertex;
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, pixel.GetAddressOf()));
		Util::SetResourceName(pixel.Get(), "GrassAlphaTest::PS");
		Check(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, vertex.GetAddressOf()));
		Util::SetResourceName(vertex.Get(), "GrassAlphaTest::VS");
		Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		context->ClearState();
		std::vector<std::unique_ptr<D3D11ShaderTest::ConstantBuffer>> constants;
		D3D11_SHADER_DESC shader{};
		Check(reflection->GetDesc(&shader));
		D3D11ShaderTest::ConstantBuffer* features = nullptr;
		D3D11ShaderTest::ConstantBuffer* alphaReference = nullptr;
		for (UINT i = 0; i < shader.ConstantBuffers; ++i) {
			D3D11_SHADER_BUFFER_DESC buffer{};
			Check(reflection->GetConstantBufferByIndex(i)->GetDesc(&buffer));
			auto& value = constants.emplace_back(std::make_unique<D3D11ShaderTest::ConstantBuffer>(device.Get(), reflection.Get(), buffer.Name));
			if (std::strcmp(buffer.Name, vr ? "VRValues" : "AlphaTestRefCB") == 0)
				alphaReference = value.get();
			if (std::strcmp(buffer.Name, "SharedData::FeatureData") == 0)
				features = value.get();
			value->Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
		}
		Require(!(depth || alpha) || alphaReference, "Alpha-tested grass must bind the engine threshold");
		context->VSSetShader(vertex.Get(), nullptr, 0);
		context->PSSetShader(pixel.Get(), nullptr, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		std::array<ID3D11RenderTargetView*, 4> outputs{};
		outputs[depth ? 0 : 3] = rtv.Get();
		context->OMSetRenderTargets(static_cast<UINT>(outputs.size()), outputs.data(), nullptr);
		D3D11_VIEWPORT viewport{ 0, 0, 4, 1, 0, 1 };
		context->RSSetViewports(1, &viewport);
		ID3D11SamplerState* sample = sampler.Get();
		context->PSSetSamplers(0, 1, &sample);
		unsigned cases = 0;
		for (UINT path = 0; path < (enhanced ? 3u : 1u); ++path) {
			if (features) {
				features->SetMember("SharedData::grassLightingSettings", "Enabled", path != 0 ? 1u : 0u);
				features->SetMember("SharedData::grassLightingSettings", "ComplexGrassThreshold", 0.1f);
				// Ordinary materials retain legacy shading even when True PBR is enabled.
				features->SetMember("SharedData::truePBRSettings", "Enabled", pbr ? 1u : 0u);
				features->Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
			}
			ID3D11ShaderResourceView* texture = textures[path == 2 ? 1 : 0].Get();
			context->PSSetShaderResources(0, 1, &texture);
			for (float threshold : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
				if (alphaReference) {
					alphaReference->SetVariable("AlphaTestRefRS", threshold);
					alphaReference->Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
				}
				constexpr float clear[4]{};
				context->ClearRenderTargetView(rtv.Get(), clear);
				context->Draw(24, 0);
				context->CopyResource(staging.Get(), target.Get());
				D3D11_MAPPED_SUBRESOURCE mapped{};
				Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
				std::array<Pixel, 4> pixels{};
				std::memcpy(pixels.data(), mapped.pData, sizeof(pixels));
				context->Unmap(staging.Get(), 0);
				constexpr std::array<float, 4> fadedAlpha{ 0.25f, 0.75f, 0.375f, 0.5f };
				for (UINT texel = 0; texel < pixels.size(); ++texel) {
					const bool covered = !(depth || alpha) || fadedAlpha[texel] >= threshold;
					const float expectedAlpha = covered ? (depth ? fadedAlpha[texel] : 1.0f) : 0.0f;
					if (pixels[texel][3] != expectedAlpha)
						throw std::runtime_error("Grass alpha coverage mismatch: VR=" + std::to_string(vr) +
												 " GO=" + std::to_string(optimized) + " enhanced=" + std::to_string(enhanced) +
												 " PBR=" + std::to_string(pbr) + " depth=" + std::to_string(depth) +
												 " alpha=" + std::to_string(alpha) + " path=" + std::to_string(path) +
												 " threshold=" + std::to_string(threshold) + " texel=" + std::to_string(texel));
				}
				++cases;
			}
		}
		return cases;
	}
};

unsigned CheckGrassAlphaCoverage()
{
	GrassAlphaFixture fixture;
	unsigned cases = 0;
	for (bool vr : { false, true })
		for (bool optimized : { false, true })
			for (UINT mode = 0; mode < 3; ++mode)
				for (bool depth : { false, true })
					for (bool alpha : { false, true })
						cases += fixture.CheckVariant(vr, optimized, mode != 0, mode == 2, depth, alpha);
	return cases;
}
