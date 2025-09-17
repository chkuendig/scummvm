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

#include "hpl1/engine/impl/CGProgram.h"

#ifdef HPL1_USE_OPENGL

#include "hpl1/engine/impl/SDLTexture.h"
#include "hpl1/engine/system/low_level_system.h"

#include "hpl1/engine/math/Math.h"
#include "hpl1/engine/system/String.h"
#include "hpl1/engine/scene/Camera3D.h"

#include "common/array.h"
#include "common/str.h"
#include "hpl1/debug.h"
#include "math/matrix4.h"

namespace hpl {

// Static camera context for OpenGL ES compatibility
cCamera3D *cCGProgram::s_pCurrentCamera = nullptr;

static OpenGL::Shader *createShader(const char *vertex, const char *fragment) {
	const char *attributes[] = {nullptr};
	return OpenGL::Shader::fromFiles(vertex, fragment, attributes);
}

static void setSamplers(OpenGL::Shader *shader) {
	shader->use();
	shader->setUniform("tex0", 0);
	shader->setUniform("tex1", 1);
	shader->setUniform("tex2", 2);
	shader->setUniform("tex3", 3);
	shader->setUniform("tex4", 4);
	shader->setUniform("tex5", 5);
	shader->setUniform("tex6", 6);
	shader->unbind();
}

cCGProgram::cCGProgram(const tString &vertex, const tString &fragment)
	: iGpuProgram(vertex + " " + fragment), _shader(createShader(vertex.c_str(), fragment.c_str())) {

	setSamplers(_shader);
}

cCGProgram::~cCGProgram() {
	delete _shader;
}

//-----------------------------------------------------------------------

bool cCGProgram::reload() {
	return false;
}

//-----------------------------------------------------------------------

void cCGProgram::unload() {
}

//-----------------------------------------------------------------------

void cCGProgram::destroy() {
	delete _shader;
}

//-----------------------------------------------------------------------

void cCGProgram::Bind() {
	Hpl1::logInfo(Hpl1::kDebugShaders, "binding shader %s\n", GetName().c_str());
	_shader->use();
}

//-----------------------------------------------------------------------

void cCGProgram::UnBind() {
	Hpl1::logInfo(Hpl1::kDebugShaders, "unbinding shader %s\n", GetName().c_str());
	_shader->unbind();
}

//-----------------------------------------------------------------------

bool cCGProgram::SetFloat(const tString &asName, float afX) {
	_shader->setUniform1f(asName.c_str(), afX);
	return true;
}

//-----------------------------------------------------------------------

bool cCGProgram::SetVec2f(const tString &asName, float afX, float afY) {
	_shader->setUniform(asName.c_str(), {afX, afY});
	return true;
}

//-----------------------------------------------------------------------

bool cCGProgram::SetVec3f(const tString &asName, float afX, float afY, float afZ) {
	_shader->setUniform(asName.c_str(), {afX, afY, afZ});
	return true;
}

//-----------------------------------------------------------------------

bool cCGProgram::SetVec4f(const tString &asName, float afX, float afY, float afZ, float afW) {
	_shader->setUniform(asName.c_str(), {afX, afY, afZ, afW});
	return true;
}

//-----------------------------------------------------------------------

bool cCGProgram::SetMatrixf(const tString &asName, const cMatrixf &mMtx) {
	Math::Matrix4 mat4;
	mat4.setData(mMtx.v);
	mat4.transpose();
	_shader->setUniform(asName.c_str(), mat4);
	return true;
}

//-----------------------------------------------------------------------

bool cCGProgram::SetMatrixf(const tString &asName, eGpuProgramMatrix mType,
							eGpuProgramMatrixOp mOp) {
	if (mType != eGpuProgramMatrix_ViewProjection) {
		error("unsupported shader matrix %d", mType);
		return false;
	}
	
	if (mOp != eGpuProgramMatrixOp_Identity) {
		error("unsupported shader matrix operation %d", mOp);
		return false;
	}
	
	// OpenGL ES doesn't support glGetFloatv with GL_MODELVIEW_MATRIX or GL_PROJECTION_MATRIX
	// Instead, use the camera context to compute the view-projection matrix
	if (s_pCurrentCamera == nullptr) {
		// Fallback to identity matrix if no camera context is available
		Math::Matrix4 identity;
		identity.setToIdentity();
		_shader->setUniform(asName.c_str(), identity);
		Hpl1::logWarning(Hpl1::kDebugShaders, "%s", "CGProgram::SetMatrixf: No camera context available, using identity matrix");
		return true;
	}
	
	// Calculate view-projection matrix using current camera
	const cMatrixf &viewMatrix = s_pCurrentCamera->GetViewMatrix();
	const cMatrixf &projMatrix = s_pCurrentCamera->GetProjectionMatrix();
	
	// Combine projection and view matrices (note: HPL uses column-major matrices)
	cMatrixf viewProjMatrix = cMath::MatrixMul(projMatrix, viewMatrix);
	
	// Convert to Math::Matrix4 format and set uniform
	Math::Matrix4 mat4;
	mat4.setData(viewProjMatrix.v);
	mat4.transpose(); // HPL matrices are row-major, OpenGL expects column-major
	_shader->setUniform(asName.c_str(), mat4);
	
	return true;
}

} // namespace hpl

#endif // HPL1_USE_OPENGL
