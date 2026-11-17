#include "std_include.hpp"
#include "component/vr/gameplay/tracer_material.hpp"
#include <cmath>
#include <mutex>

namespace d3d11
{
	std::unique_lock<std::recursive_mutex> acquire_gpu_queue_interop(gpu_queue_client)
	{ static std::recursive_mutex gate;return std::unique_lock(gate); }
}

int main()
{
	using Microsoft::WRL::ComPtr;
	namespace material=vr::gameplay::weapons::tracer_material;
	unsigned failures{};
	const auto check=[&](bool ok,const char* message){if(!ok){++failures;std::cerr<<message<<'\n';}};
	// The test process owns this otherwise-free address range. No game process
	// is attached; this supplies the same native mutex slot used in production.
	constexpr auto address=std::uintptr_t(0x141160000);
	MEMORY_BASIC_INFORMATION memory{};
	if(!VirtualQuery(reinterpret_cast<void*>(address),&memory,sizeof(memory)) || memory.State!=MEM_FREE)return 2;
	void* reserved=VirtualAlloc(reinterpret_cast<void*>(address),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
	if(reserved!=reinterpret_cast<void*>(address))return 2;
	const auto release=gsl::finally([&]{material::clear_after_drain();VirtualFree(reserved,0,MEM_RELEASE);});
	ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
	if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return 2;
	const std::array<float,16> pixels{4,1,.25f,.75f,.1f,2,.5f,.25f,.4f,.2f,8,1,0,0,0,0};
	const std::array<float,4> mip_pixel{.25f,.75f,.5f,.5f};
	D3D11_TEXTURE2D_DESC desc{2,2,2,1,DXGI_FORMAT_R32G32B32A32_FLOAT,{1,0},D3D11_USAGE_IMMUTABLE,D3D11_BIND_SHADER_RESOURCE,0,0};
	const D3D11_SUBRESOURCE_DATA initial[]{{pixels.data(),32,0},{mip_pixel.data(),16,0}};
	ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;
	if(FAILED(device->CreateTexture2D(&desc,initial,&texture)) || FAILED(device->CreateShaderResourceView(texture.Get(),nullptr,&view)))return 2;
	game::GfxImage image{};image.texture.map=texture.Get();image.texture.shaderView=view.Get();image.imageFormat=desc.Format;
	image.width=image.height=2;image.levelCount=2;
	game::MaterialTextureDef binding{};binding.u.image=&image;
	game::Material original{};original.textureCount=1;original.textureTable=&binding;
	check(!material::request(&original),"A pending material is never published before its GPU pixels exist");
	d3d11::present_event event{};event.graphics.device=device;event.graphics.context=context;event.graphics.generation=1;
	material::prepare(event);
	check(!material::request(&original),"Missing native context ownership leaves the request pending");
	const HANDLE native=CreateMutexW(nullptr,FALSE,nullptr);
	if(!native)return 2;
	const auto close=gsl::finally([&]{CloseHandle(native);});
	*reinterpret_cast<HANDLE*>(0x141167910)=native;
	auto* previous=view.Get();context->CSSetShaderResources(0,1,&previous);
	material::prepare(event);
	auto* colored=material::request(&original);
	check(colored && colored!=&original,"A private H1 material is published after conversion");
	if(!colored)return 1;
	check(original.textureTable==&binding && binding.u.image==&image && image.texture.shaderView==view.Get(),"Original NPC/Native material and image remain untouched");
	ComPtr<ID3D11ShaderResourceView> restored;context->CSGetShaderResources(0,1,&restored);
	check(restored.Get()==view.Get(),"Immediate context bindings survive the private command list");
	const auto* converted=colored->textureTable[0].u.image;
	D3D11_TEXTURE2D_DESC result{};converted->texture.map->GetDesc(&result);
	check(result.Format==DXGI_FORMAT_R16G16B16A16_FLOAT && result.MipLevels==2,"HDR precision and the full mip chain survive");
	result.Usage=D3D11_USAGE_STAGING;result.BindFlags=0;result.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Texture2D> staging;
	if(FAILED(device->CreateTexture2D(&result,nullptr,&staging)))return 2;
	context->CopyResource(staging.Get(),converted->texture.map);
	const auto half=[](std::uint16_t bits){
		const unsigned exponent=(bits>>10)&31, fraction=bits&1023;
		return (bits&0x8000?-1.f:1.f)*(exponent?std::ldexp(1.f+fraction/1024.f,int(exponent)-15):std::ldexp(float(fraction),-24));
	};
	for(unsigned mip=0;mip<2;++mip)
	{
		D3D11_MAPPED_SUBRESOURCE read{};
		if(FAILED(context->Map(staging.Get(),mip,D3D11_MAP_READ,0,&read)))return 2;
		const unsigned width=mip?1:2;
		for(unsigned y=0;y<width;++y)for(unsigned x=0;x<width;++x)
		{
			const auto* sample=reinterpret_cast<const std::uint16_t*>(static_cast<const std::byte*>(read.pData)+y*read.RowPitch)+4*x;
			const auto* source=mip?mip_pixel.data():pixels.data()+4*(y*width+x);
			const float envelope=std::max({source[0],source[1],source[2]});
			for(unsigned channel=0;channel<4;++channel)
				check(std::abs(half(sample[channel])-(channel==3?source[3]:envelope))<.001f,"Readback retains each mip's HDR envelope and alpha");
		}
		context->Unmap(staging.Get(),mip);
	}
	std::cout<<"MWR_TRACER_HDR_FAILURES="<<failures<<'\n';return failures?1:0;
}
