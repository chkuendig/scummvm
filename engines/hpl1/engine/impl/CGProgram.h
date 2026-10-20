/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
 * Copyright (C) 2006-2010 - Frictional Games
 *
 * This file is part of HPL1 Engine.
 */

#ifndef HPL_CGPROGRAM_H
#define HPL_CGPROGRAM_H

#include "common/scummsys.h"
#include "hpl1/engine/graphics/GPUProgram.h"
#include "hpl1/engine/math/MathTypes.h"
#include "hpl1/engine/system/SystemTypes.h"
#include "hpl1/opengl.h"

#ifdef HPL1_USE_OPENGL

#include "graphics/opengl/shader.h"

namespace hpl {

#if USE_FORCED_GLES2
class cLowLevelGraphicsGLES;
#endif

class cCGProgram : public iGpuProgram {
public:
	cCGProgram(const tString &vertex, const tString &fragment);
	~cCGProgram();

	bool reload();
	void unload();
	void destroy();

	tString GetProgramName() { return msName; }

	void Bind();
	void UnBind();

	bool SetFloat(const tString &asName, float afX);
	bool SetVec2f(const tString &asName, float afX, float afY);
	bool SetVec3f(const tString &asName, float afX, float afY, float afZ);
	bool SetVec4f(const tString &asName, float afX, float afY, float afZ, float afW);

	bool SetMatrixf(const tString &asName, const cMatrixf &mMtx);
	bool SetMatrixf(const tString &asName, eGpuProgramMatrix mType,
					eGpuProgramMatrixOp mOp);

#if USE_FORCED_GLES2
	// GLES2 has no glGetFloatv(GL_MODELVIEW_MATRIX). cCGProgram::SetMatrixf
	// (ViewProjection, Identity) reads the matrix stack from the LowLevel
	// renderer instead — set this once from cLowLevelGraphicsGLES::Init.
	static void SetCurrentLowLevel(cLowLevelGraphicsGLES *apLowLevel) { s_pLowLevel = apLowLevel; }
#endif

#if USE_FORCED_GLES2
	// GLES2 has no non-normalized Rect sampler; cCGProgram polyfills
	// texture2DRect() with normalized UVs and needs the framebuffer size. Set
	// on screen/virtual-size changes; Bind() pushes the reciprocal as a uniform.
	static void SetCurrentFramebufferSize(int aWidth, int aHeight) {
		s_framebufferWidth = aWidth;
		s_framebufferHeight = aHeight;
	}
#endif

private:
	OpenGL::Shader *_shader;

	tString msName;
	tString msFile;
	tString msEntry;

#if USE_FORCED_GLES2
	static cLowLevelGraphicsGLES *s_pLowLevel;
	static int s_framebufferWidth;
	static int s_framebufferHeight;
#endif
};

} // namespace hpl

#endif // HPL1_USE_OPENGL
#endif // HPL_CGPROGRAM_H
