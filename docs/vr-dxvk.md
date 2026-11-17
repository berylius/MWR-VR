# Vulkan renderer (DXVK)

Optional workaround for AMD driver rendering faults: the game's Direct3D 11
runs on Vulkan through DXVK 3.1.1 (bundled, `overlord-mwr/dxvk/`, zlib license).

## Turning it on

Launcher > Basics > VR backend > **Vulkan renderer (DXVK)** (`vr_dxvk`, off by
default). The game reads the option at startup and loads the bundled DXVK by
full path (`component/dxvk_runtime.hpp`), only for its own `CreateDXGIFactory` /
`D3D11CreateDevice` imports (`component/d3d11.cpp`). The process-wide
`d3d11.dll`/`dxgi.dll` stay the system ones, so in-process OpenXR runtimes that
make their own D3D11 device for Vulkan interop keep real D3D11. Virtual
Desktop's VDXR does (shared `ID3D11Fence` imported as a Vulkan semaphore);
with DXVK as the game folder's `d3d11.dll` that fence failed and
`xrCreateSession` returned `XR_ERROR_RUNTIME_FAILURE`.

DXVK's `d3d11.dll` imports `CreateDXGIFactory1` by the name `dxgi.dll`, which
binds to the system DXGI; `dxvk_runtime` repoints that import to DXVK's DXGI.
The launcher (`launcher/dxvk_install.cpp`) removes DXVK copies older builds
placed in the game folder (only byte-identical ones) and checks the bundle when
the option is on. `OVERLORD_MWR_DXVK=0/1` overrides the setting for tests.

The first start after switching shows the game's "hardware changed" settings
prompt, because DXVK reports its own adapter identity.

## How VR presentation works with DXVK

DXVK cannot share D3D11 textures with another device on Windows
(`OpenSharedResource` returns `E_INVALIDARG`, even DXVK to DXVK), so the
runtime's own D3D11 device cannot open our swapchain images. With a DXVK game
device the OpenXR backend therefore uses `XR_KHR_vulkan_enable`
(`component/vr/openxr_vulkan.cpp`):

- `IDXGIVkInteropDevice1` gives DXVK's `VkInstance`, `VkPhysicalDevice`,
  `VkDevice` and graphics queue; the session binds them
  (`XrGraphicsBindingVulkanKHR`). The runtime's `xrGetVulkanGraphicsDeviceKHR`
  must name the same GPU, otherwise initialization stops with a clear error.
- Swapchains use the VkFormat of the chosen DXGI format; every runtime `VkImage`
  is wrapped with `CreateTexture2DFromVkImage` as a render-target-only texture,
  so DXVK keeps it in `COLOR_ATTACHMENT_OPTIMAL`, the layout OpenXR uses.
  The rest of the D3D11 presentation path is unchanged.
- xrCreate/DestroySwapchain, xrAcquire/ReleaseSwapchainImage and
  xrBegin/EndFrame hold DXVK's submission-queue lock; release and end frame
  flush DXVK's recorded work first.

The OpenVR backend submits D3D11 textures to SteamVR and refuses a DXVK device.

## Verification

Mock only (no headset): mockxr gained `XR_KHR_vulkan_enable` (VkImage swapchains
on the app device, Vulkan readback for captures). Killhouse: transport
`dxvk_vulkan_openxr`, thousands of frames, 0 failures, both eyes correct; D3D11
path unchanged (`h2_device_openxr`). Real runtimes (Meta, SteamVR, Virtual
Desktop) and AMD hardware still need a headset check.
