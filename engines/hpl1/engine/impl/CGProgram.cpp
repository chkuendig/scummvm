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

#if USE_FORCED_GLES2
#include "hpl1/engine/impl/LowLevelGraphicsGLES.h"
#else
#include "hpl1/engine/impl/LowLevelGraphicsSDL.h"
#endif
#include "hpl1/engine/impl/SDLTexture.h"
#include "hpl1/engine/system/low_level_system.h"

#include "hpl1/engine/math/Math.h"
#include "hpl1/engine/system/String.h"

#include "common/archive.h"
#include "common/array.h"
#include "common/config-manager.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/str.h"
#include "hpl1/debug.h"
#include "math/matrix4.h"

namespace hpl {

#if USE_FORCED_GLES2
cLowLevelGraphicsGLES *cCGProgram::s_pLowLevel = nullptr;
#endif

#if USE_FORCED_GLES2
// Updated by cLowLevelGraphicsGLES::Init / SetVirtualSize. Used to feed the
// polyfilled texture2DRect()'s normalized coordinate calculation.
int cCGProgram::s_framebufferWidth = 800;
int cCGProgram::s_framebufferHeight = 600;
#endif

#if USE_FORCED_GLES2
// HPL1 shaders use legacy GLSL fixed-function built-ins (gl_Vertex/gl_Color/
// gl_Normal/gl_MultiTexCoord*) that don't exist under WebGL2/GLSL ES 1.00. We
// prepend a compat preamble (shaders/hpl1_gles2compat.{fragment,vertex}) that
// declares matching _hpl1_* attributes, #defines the gl_* names to them, and
// polyfills missing samplers/functions; ScummVM's shader.cpp handles the
// in/out -> attribute/varying mapping. Preamble is loaded once and cached.

static bool usesLegacyGLSL(const tString &src) {
	return src.contains("gl_Vertex") || src.contains("gl_Color")
		|| src.contains("gl_Normal") || src.contains("gl_MultiTexCoord");
}

// HPL1 vertex shaders use global-scope initializers like `vec4 position =
// gl_Vertex;`, which GLSL ES 1.00 forbids (global initializers must be
// constant). Lift any `<vecType> <ident> = gl_*;` line into main()'s body so
// the attribute read happens in function scope.
static tString liftGlobalAttributeInits(const tString &src) {
	tString hoisted;
	tString rest;
	size_t pos = 0;
	while (pos < src.size()) {
		const size_t lineEnd = src.findFirstOf('\n', pos);
		const size_t endIdx = (lineEnd == Common::String::npos) ? src.size() : lineEnd + 1;
		const tString line(src.c_str() + pos, endIdx - pos);
		// Pattern: leading whitespace, then "vecN ident = gl_*<...>;"
		size_t cursor = 0;
		while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t'))
			++cursor;
		const bool isVecDecl =
			cursor + 4 < line.size() && line[cursor] == 'v' && line[cursor + 1] == 'e' &&
			line[cursor + 2] == 'c' && line[cursor + 3] >= '2' && line[cursor + 3] <= '4' &&
			line[cursor + 4] == ' ';
		if (isVecDecl && line.contains("= gl_") && line.contains(";")) {
			// Promote the line to main()-scope.
			hoisted += "\t";
			hoisted += line.c_str() + cursor;
		} else {
			rest += line;
		}
		pos = endIdx;
	}
	if (hoisted.empty())
		return src;
	// Inject after the `void main()`-opening brace. Tolerate `main()`, `main( )`,
	// `main(void)`, and either same-line `{` or next-line `{`.
	const size_t mainIdx = rest.find("void main");
	if (mainIdx == Common::String::npos)
		return src; // give up — caller will see the error from the GL compiler
	const size_t braceIdx = rest.findFirstOf('{', mainIdx);
	if (braceIdx == Common::String::npos)
		return src;
	tString out;
	out += tString(rest.c_str(), braceIdx + 1);
	out += "\n";
	out += hoisted;
	out += tString(rest.c_str() + braceIdx + 1);
	return out;
}

// Shader-side replacement for glAlphaFunc/GL_ALPHA_TEST (absent on GLES2):
// inject `if (outColor.a < _hpl1_alphaRef) discard;` before main()'s closing
// brace. _hpl1_alphaRef defaults to 0 so the test never fires unless
// cLowLevelGraphicsGLES::SetAlphaTestFunc pushes a real threshold.
static tString injectAlphaTest(const tString &src) {
	const size_t mainIdx = src.find("void main");
	if (mainIdx == Common::String::npos)
		return src;
	const size_t openIdx = src.findFirstOf('{', mainIdx);
	if (openIdx == Common::String::npos)
		return src;
	// Find the matching closing brace by counting depth.
	int depth = 1;
	size_t cursor = openIdx + 1;
	while (cursor < src.size() && depth > 0) {
		if (src[cursor] == '{')
			++depth;
		else if (src[cursor] == '}')
			--depth;
		if (depth == 0)
			break;
		++cursor;
	}
	if (cursor >= src.size() || depth != 0)
		return src;
	tString out;
	out += tString(src.c_str(), cursor);
	out += "\n\tif (outColor.a < _hpl1_alphaRef) discard;\n";
	out += tString(src.c_str() + cursor);
	return out;
}

static tString readShaderSource(const tString &baseName, const char *ext) {
	Common::Path shaderDir("shaders/", '/');
	const tString filename = baseName + "." + ext;
#if !defined(RELEASE_BUILD) && !defined(EMSCRIPTEN)
	// Dev convenience: when launched from the repo root, resolve unbundled
	// shaders under engines/hpl1/engine/impl/. Excluded on EMSCRIPTEN, where
	// the wasm FS has no source tree (shaders are bundled at /data/shaders/)
	// and addDirectory would warn on every shader load.
	SearchMan.addDirectory("HPL1_SHADERS", "engines/hpl1/engine/impl", 0, 2);
#endif
	if (ConfMan.hasKey("extrapath"))
		SearchMan.addDirectory("EXTRA_PATH", Common::FSNode(ConfMan.getPath("extrapath")), 0, 2);
	Common::File f;
	const bool opened = f.open(shaderDir.appendComponent(filename));
#if !defined(RELEASE_BUILD) && !defined(EMSCRIPTEN)
	SearchMan.remove("HPL1_SHADERS");
#endif
	SearchMan.remove("EXTRA_PATH");
	if (!opened)
		error("[HPL1-GLES2] could not open shader '%s'", filename.c_str());
	const int32 size = f.size();
	Common::Array<char> buf(size + 1);
	f.read(buf.data(), size);
	f.close();
	buf[size] = '\0';
	return liftGlobalAttributeInits(tString(buf.data()));
}

// Load and cache a compat preamble file; content is shared across shaders so
// read it only once.
static const tString &loadCachedPreamble(const char *ext, tString &cache) {
	if (!cache.empty())
		return cache;
	Common::Path shaderDir("shaders/", '/');
	const tString filename = tString("hpl1_gles2compat.") + ext;
#if !defined(RELEASE_BUILD) && !defined(EMSCRIPTEN)
	SearchMan.addDirectory("HPL1_SHADERS", "engines/hpl1/engine/impl", 0, 2);
#endif
	Common::File f;
	const bool opened = f.open(shaderDir.appendComponent(filename));
#if !defined(RELEASE_BUILD) && !defined(EMSCRIPTEN)
	SearchMan.remove("HPL1_SHADERS");
#endif
	if (!opened)
		error("[HPL1-GLES2] could not open compat preamble '%s'", filename.c_str());
	const int32 size = f.size();
	Common::Array<char> buf(size + 1);
	f.read(buf.data(), size);
	f.close();
	buf[size] = '\0';
	cache = tString(buf.data());
	return cache;
}

// The "common" preamble lives in hpl1_gles2compat.fragment but is prepended to
// both stages; its sampler/texture polyfills are harmless in vertex code.
static const tString &loadCommonPreamble() {
	static tString cache;
	return loadCachedPreamble("fragment", cache);
}

static const tString &loadVertexPreamble() {
	static tString cache;
	return loadCachedPreamble("vertex", cache);
}
#endif // USE_FORCED_GLES2

static OpenGL::Shader *createShader(const char *vertex, const char *fragment) {
#if USE_FORCED_GLES2
	const tString vertexBody = readShaderSource(vertex, "vertex");
	const tString fragmentBody = readShaderSource(fragment, "fragment");
	const bool legacy = usesLegacyGLSL(vertexBody);

	const char *kAttributes[6] = {nullptr};
	if (legacy) {
		// Bind the names introduced by the compat preamble.
		kAttributes[eVtxAttr_Position] = "_hpl1_pos";
		kAttributes[eVtxAttr_Color0] = "_hpl1_color";
		kAttributes[eVtxAttr_Normal] = "_hpl1_normal";
		kAttributes[eVtxAttr_Texture0] = "_hpl1_uv";
		kAttributes[eVtxAttr_Tangent] = "_hpl1_tangent";
	} else {
		// Modern GLSL-ES style shader (hpl1_Simple, hpl1_gamma_correction)
		// that declares its own a_position / a_uv / a_color attributes.
		kAttributes[eVtxAttr_Position] = "a_position";
		kAttributes[eVtxAttr_Color0] = "a_color";
		kAttributes[eVtxAttr_Normal] = "a_normal";
		kAttributes[eVtxAttr_Texture0] = "a_uv";
		kAttributes[eVtxAttr_Tangent] = "a_tangent";
	}

	const tString vertexSrc = legacy
		? loadCommonPreamble() + loadVertexPreamble() + vertexBody
		: vertexBody;
	const tString fragmentSrc = loadCommonPreamble() + injectAlphaTest(fragmentBody);

	// loadFromStrings (not fromStrings) returns false on failure instead of
	// aborting, so we can log both shader sources before bailing out.
	OpenGL::Shader *s = new OpenGL::Shader();
	const Common::String shaderName = tString(vertex) + "/" + fragment;
	if (!s->loadFromStrings(shaderName, vertexSrc.c_str(), fragmentSrc.c_str(),
							kAttributes,
							120 /* compatGLSLVersion */)) {
		const tString err = s->getError();
		delete s;
		warning("[HPL1-GLES2] === vertex source for '%s' ===\n%s",
		        shaderName.c_str(), vertexSrc.c_str());
		warning("[HPL1-GLES2] === fragment source for '%s' ===\n%s",
		        shaderName.c_str(), fragmentSrc.c_str());
		error("[HPL1-GLES2] shader '%s' failed to compile: %s",
		      shaderName.c_str(), err.c_str());
	}
	return s;
#else
	const char *attributes[] = {nullptr};
	return OpenGL::Shader::fromFiles(vertex, fragment, attributes);
#endif
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
	// Named-sampler uniforms for the simple/gamma shaders; harmless elsewhere
	// (setUniform ignores missing names).
	shader->setUniform("diffuseMap", 0);
	shader->setUniform("normalMap", 1);
	shader->setUniform("normalCubeMap", 2);
	shader->setUniform("falloffMap", 3);
	shader->setUniform("spotlightMap", 4);
	shader->setUniform("spotNegRejectMap", 5);
	shader->setUniform("specularMap", 6);
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
#if USE_FORCED_GLES2
	// Refresh the framebuffer-size uniform every Bind() so canvas resizes take
	// effect; shaders without _hpl1_invFramebufferSize just drop it.
	const float invW = s_framebufferWidth  > 0 ? 1.0f / (float)s_framebufferWidth  : 0.0f;
	const float invH = s_framebufferHeight > 0 ? 1.0f / (float)s_framebufferHeight : 0.0f;
	_shader->setUniform("_hpl1_invFramebufferSize", {invW, invH});
	// Tell the renderer which program is live (DrawQuad fallback / matrix upload).
	if (s_pLowLevel)
		s_pLowLevel->NotifyShaderBound(this);
#endif
}

//-----------------------------------------------------------------------

void cCGProgram::UnBind() {
	Hpl1::logInfo(Hpl1::kDebugShaders, "unbinding shader %s\n", GetName().c_str());
	_shader->unbind();
#if USE_FORCED_GLES2
	if (s_pLowLevel)
		s_pLowLevel->NotifyShaderUnbound(this);
#endif
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
	if (mType != eGpuProgramMatrix_ViewProjection)
		error("unsupported shader matrix %d", mType);

#if USE_FORCED_GLES2
	if (mOp != eGpuProgramMatrixOp_Identity)
		error("unsupported shader matrix operation %d", mOp);

	// GLES2 has no fixed-function matrices, so build worldViewProj from
	// cLowLevelGraphicsGLES's software matrix stack (the engine has pushed the
	// per-object world transform onto eMatrix_ModelView). Without this every
	// object collapses to a degenerate triangle.
	if (s_pLowLevel == nullptr)
		error("cCGProgram::SetMatrixf(ViewProjection): no LowLevelGraphics");
	const cMatrixf modelView = s_pLowLevel->GetMatrixStackTop(eMatrix_ModelView);
	const cMatrixf projection = s_pLowLevel->GetMatrixStackTop(eMatrix_Projection);
	const cMatrixf worldViewProj = cMath::MatrixMul(projection, modelView);

	Math::Matrix4 mat4;
	mat4.setData(worldViewProj.v);
	mat4.transpose();
	_shader->setUniform(asName.c_str(), mat4);
#else
	Math::Matrix4 modelView, projection;
	glGetFloatv(GL_PROJECTION_MATRIX, projection.getData());
	glGetFloatv(GL_MODELVIEW_MATRIX, modelView.getData());
	_shader->setUniform(asName.c_str(), modelView * projection);
#endif

	return true;
}

} // namespace hpl

#endif // HPL1_USE_OPENGL
