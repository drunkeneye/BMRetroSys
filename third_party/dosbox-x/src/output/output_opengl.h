/* output_opengl.h shim - OpenGL output is a Phase-1 non-goal (C_OPENGL off
 * on BMX). dos_programs.cpp includes this header unconditionally but only
 * uses it inside `#if C_OPENGL && DOSBOXMENU_TYPE == DOSBOXMENU_SDLDRAW`,
 * so an empty shim satisfies compilation. */
#ifndef BMX_OUTPUT_OPENGL_H_SHIM_
#define BMX_OUTPUT_OPENGL_H_SHIM_

#endif /* BMX_OUTPUT_OPENGL_H_SHIM_ */
