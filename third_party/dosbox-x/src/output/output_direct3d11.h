// output_direct3d11.h stub — BMX bare-metal has no Direct3D.
//
// Upstream src/output/output_direct3d11.h unconditionally includes
// <windows.h>/<d3d11.h> plus the whole output header chain. Nothing in the
// Phase-1 subset uses any D3D11 symbol (vga_draw.cpp only includes it for
// the OUTPUT_DIRECT3D11_* API which is C_DIRECT3D-gated and off in our
// config.h), so this empty stub satisfies the preprocessor. If a future
// phase needs the real header, vendor it plus output_direct3d.h,
// output_surface.h and output_tools_xbrz.h.
#pragma once
