# Shader preloading

MWR VR always disables `r_preloadShaders`,
`r_preloadShadersFrontendAllow`, and `r_preloadShadersAfterCinematic`.
This applies to both native D3D11 and DXVK, including non-VR startup in
the mod. There is no setting to enable shader preloading.

The native hardware-optimization prompt can re-enable level shader
preloading even when the profile disabled it. Reproduction on 2026-11-16:
DXVK off, accept optimization, then `devmap bog_a` fails during loading
with `IDXGISwapChain::Present failed`, device-removed reason `0x887A0005`.
With the same optimization and profile, setting only `r_preloadShaders 0`
before that map load reaches playable stereo rendering without device loss.
The longer loading reported in GitHub issue #8 has not been reproduced
on the reporter's hardware; this fixes the locally reproduced failure.

The three native booleans have false defaults and read-only saved flags.
A byte-checked MWR `Dvar_SetVariant` hook at `0x14041C190` also coerces
their values to false for internal setters, so optimization can no longer
re-enable them. Other dvars retain the original setter. The normal driver
shader cache and on-demand shader creation remain in use.

`vr_status` and `vr_shader_preload_status` report the installed policy and
the number of blocked enable attempts. The ordinary effective-settings
section also reports all three values.
