#include <std_include.hpp>
#include "tracer_material.hpp"
#include <d3dcompiler.h>
#include <map>
#include <mutex>

namespace vr::gameplay::weapons::tracer_material
{
	namespace
	{
		using Microsoft::WRL::ComPtr;
		struct entry
		{
			game::Material material{};
			game::MaterialTextureDef binding{};
			game::GfxImage image{};
			ComPtr<ID3D11ShaderResourceView> original, view;
			ComPtr<ID3D11Texture2D> texture;
			bool ready{}, rejected{};
		};
		std::mutex mutex;
		using identity=std::pair<game::Material*,ID3D11ShaderResourceView*>;
		std::map<identity,std::unique_ptr<entry>> entries;
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11ComputeShader> shader;
		constexpr const char* code=R"(
Texture2D<float4> inputImage : register(t0);
RWTexture2D<float4> outputImage : register(u0);
[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID) {
 uint width,height; outputImage.GetDimensions(width,height);
 if(p.x>=width || p.y>=height)return;
 float4 value=inputImage.Load(int3(p.xy,0));
 float envelope=max(value.r,max(value.g,value.b));
 outputImage[p.xy]=float4(envelope,envelope,envelope,value.a);
})";
		bool convert(entry& value,ID3D11Device* gpu,ID3D11DeviceContext* context)
		{
			ComPtr<ID3D11Resource> original;
			value.original->GetResource(&original);
			ComPtr<ID3D11Texture2D> source;
			if(FAILED(original.As(&source)))return false;
			D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
			if(desc.ArraySize!=1 || desc.SampleDesc.Count!=1 || !desc.MipLevels || desc.Width>1024 || desc.Height>1024)return false;
			D3D11_SHADER_RESOURCE_VIEW_DESC source_view{};value.original->GetDesc(&source_view);
			if(source_view.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D || source_view.Texture2D.MostDetailedMip)return false;
			desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.Usage=D3D11_USAGE_DEFAULT;
			desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;desc.CPUAccessFlags=desc.MiscFlags=0;
			if(FAILED(gpu->CreateTexture2D(&desc,nullptr,&value.texture)) ||
				FAILED(gpu->CreateShaderResourceView(value.texture.Get(),nullptr,&value.view)))return false;
			ComPtr<ID3D11DeviceContext> deferred;
			if(FAILED(gpu->CreateDeferredContext(0,&deferred)))return false;
			deferred->CSSetShader(shader.Get(),nullptr,0);
			for(UINT mip=0;mip<desc.MipLevels;++mip)
			{
				D3D11_SHADER_RESOURCE_VIEW_DESC input=source_view;
				input.Texture2D.MostDetailedMip=mip;input.Texture2D.MipLevels=1;
				ComPtr<ID3D11ShaderResourceView> view;
				D3D11_UNORDERED_ACCESS_VIEW_DESC output{};output.Format=desc.Format;
				output.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;output.Texture2D.MipSlice=mip;
				ComPtr<ID3D11UnorderedAccessView> target;
				if(FAILED(gpu->CreateShaderResourceView(source.Get(),&input,&view)) ||
					FAILED(gpu->CreateUnorderedAccessView(value.texture.Get(),&output,&target)))return false;
				auto* input_pointer=view.Get();auto* output_pointer=target.Get();
				deferred->CSSetShaderResources(0,1,&input_pointer);deferred->CSSetUnorderedAccessViews(0,1,&output_pointer,nullptr);
				deferred->Dispatch((std::max(1u,desc.Width>>mip)+7)/8,(std::max(1u,desc.Height>>mip)+7)/8,1);
				output_pointer=nullptr;deferred->CSSetUnorderedAccessViews(0,1,&output_pointer,nullptr);
			}
			ComPtr<ID3D11CommandList> commands;
			if(FAILED(deferred->FinishCommandList(FALSE,&commands)))return false;
			context->ExecuteCommandList(commands.Get(),TRUE);
			value.image.texture={};value.image.texture.map=value.texture.Get();
			value.image.texture.shaderView=value.image.texture.shaderViewAlternate=value.view.Get();
			value.image.imageFormat=desc.Format;value.image.streamed=0;value.image.pixelData=nullptr;
			value.binding.u.image=&value.image;value.material.textureTable=&value.binding;
			value.ready=true;return true;
		}
	}
	game::Material* request(game::Material* source)
	{
		if(!source || source->textureCount!=1 || !source->textureTable || !source->textureTable[0].u.image)return nullptr;
		const std::lock_guard lock(mutex);
		const identity key{source,source->textureTable[0].u.image->texture.shaderView};
		if(const auto found=entries.find(key);found!=entries.end())return found->second->ready?&found->second->material:nullptr;
		if(entries.size()>=128)return nullptr;
		auto value=std::make_unique<entry>();value->material=*source;value->binding=source->textureTable[0];
		value->image=*value->binding.u.image;value->original=value->image.texture.shaderView;
		if(!value->original)return nullptr;
		entries.emplace(key,std::move(value));return nullptr;
	}
	void prepare(const d3d11::present_event& event)
	{
		if(!event.graphics)return;
		{
			const std::lock_guard lock(mutex);
			if(entries.empty() || (device.Get()==event.graphics.device.Get() &&
				std::none_of(entries.begin(),entries.end(),
				[](const auto& item){return !item.second->ready && !item.second->rejected;})))return;
		}
		// Interop owners may retire materials while draining the queue. Keep
		// the same queue-before-cache order as those owners.
		const auto queue=d3d11::acquire_gpu_queue_interop();
		const std::lock_guard lock(mutex);
		if(entries.empty())return;
		if(device.Get()==event.graphics.device.Get() && std::none_of(entries.begin(),entries.end(),
			[](const auto& item){return !item.second->ready && !item.second->rejected;}))return;
		if(device.Get()!=event.graphics.device.Get())
		{
			device=event.graphics.device;shader.Reset();
			for(auto& [source,value]:entries){(void)source;value->ready=false;value->rejected=true;}
			// Requests on a newly created device follow zone/device retirement.
			for(auto& [source,value]:entries){(void)source;ComPtr<ID3D11Device> owner;value->original->GetDevice(&owner);value->rejected=owner.Get()!=device.Get();}
		}
		if(!shader)
		{
			ComPtr<ID3DBlob> binary,errors;
			if(FAILED(D3DCompile(code,std::strlen(code),nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&binary,&errors)) ||
				FAILED(device->CreateComputeShader(binary->GetBufferPointer(),binary->GetBufferSize(),nullptr,&shader)))return;
		}
		// Same verified H1 immediate-context mutex as engine_stereo_owner_pass.
		const auto native=*reinterpret_cast<HANDLE*>(0x141167910);
		if(!native)return;
		const auto waited=WaitForSingleObject(native,0);
		if(waited==WAIT_ABANDONED){ReleaseMutex(native);return;}
		if(waited!=WAIT_OBJECT_0)return;
		const auto release=gsl::finally([&]{ReleaseMutex(native);});
		for(auto& [source,value]:entries)
		{
			(void)source;
			if(!value->ready && !value->rejected)value->rejected=!convert(*value,device.Get(),event.graphics.context.Get());
		}
	}
	void clear_after_drain()
	{
		const std::lock_guard lock(mutex);entries.clear();shader.Reset();device.Reset();
	}
	std::string status()
	{
		const std::lock_guard lock(mutex);
		unsigned ready{},rejected{};
		for(const auto& [key,value]:entries){(void)key;ready+=unsigned(value->ready);rejected+=unsigned(value->rejected);}
		return std::format("tracer_hdr_materials={} ready={} rejected={}\n",entries.size(),ready,rejected);
	}
}
