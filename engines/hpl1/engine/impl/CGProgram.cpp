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
#include "hpl1/engine/scene/Camera3D.h"

#include "common/archive.h"
#include "common/array.h"
#include "common/config-manager.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/str.h"
#include "hpl1/debug.h"
#include "math/matrix4.h"

namespace hpl {

// Static camera context — GLES2 has no glGetFloatv(GL_MODELVIEW_MATRIX), and
// even on desktop the existing SetMatrixf(ViewProjection) path is cleaner
// when sourced from the active camera. Kept enabled on both targets.
cCamera3D *cCGProgram::s_pCurrentCamera = nullptr;
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
// The HPL1 .vertex/.fragment files were authored against legacy GLSL: they
// reference fixed-function attributes (gl_Vertex / gl_Color / gl_Normal /
// gl_MultiTexCoord0 / gl_MultiTexCoord1) and use GLSL 1.30-style "out"
// varyings. Under WebGL2 / GLSL ES 1.00 those built-ins do not exist. We
// prepend a compat preamble that:
//   - declares matching generic vertex attributes (_hpl1_*) bound to the
//     same slots `cLowLevelGraphicsGLES`'s batch path uses
//   - aliases the gl_* names to those attributes via #define
//   - polyfills the GLES2-missing samplers/functions used by some shaders
// The standard ScummVM shader compat layer (graphics/opengl/shader.cpp)
// handles the in/out → attribute/varying mapping when compatGLSLVersion is
// passed, so we lean on that for the rest.
//
// The actual preamble text lives in shaders/hpl1_gles2compat.{fragment,vertex}
// alongside the user shaders so it's editable in one place; loaded once on
// first use and cached.

static bool usesLegacyGLSL(const tString &src) {
	return src.contains("gl_Vertex") || src.contains("gl_Color")
		|| src.contains("gl_Normal") || src.contains("gl_MultiTexCoord");
}

// Many HPL1 vertex shaders start with global-scope assignments like
//   vec4 position = gl_Vertex;
//   vec3 normal = gl_Normal;
//   vec2 uv = gl_MultiTexCoord0.xy;
// GLSL 1.20 (desktop) allows this; GLSL ES 1.00 does not — section 4.3.5
// requires global initializers to be constant expressions. Lift any such
// lines down into the body of main() so the attribute reads happen in
// function scope. The transform is deliberately textual and limited to the
// `<type> <ident> = gl_(Vertex|Color|Normal|MultiTexCoord*)<suffix>;` pattern
// — that covers every place this idiom appears in the engine's shaders.
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

// Inject `if (outColor.a < _hpl1_alphaRef) discard;` right before main()'s
// closing brace. This is the shader-side replacement for glAlphaFunc/glEnable(
// GL_ALPHA_TEST), which don't exist on GLES2. With `_hpl1_alphaRef` defaulting
// to 0, the predicate is `< 0` and never fires, so a fragment shader that
// doesn't need alpha test pays nothing. cLowLevelGraphicsGLES::SetAlphaTestFunc
// pushes the actual threshold when the engine asks for an alpha-test pass
// (e.g. decals like the seachart that are Diffuse + UseAlpha=True on desktop).
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
	// Source-tree dev convenience: when ScummVM is launched from its own
	// repository root, the unbundled `.vertex` / `.fragment` files live at
	// engines/hpl1/engine/impl/shaders/ relative to CWD. Adding that
	// directory makes Common::File::open("shaders/X.vertex") resolve in
	// that layout too.
	//
	// Excluded from EMSCRIPTEN: the wasm filesystem doesn't contain a
	// source tree (CWD is "/", and shaders are bundled at /data/shaders/
	// which is already covered by OSystem_Emscripten::addSysArchivesToSearchSet
	// via the DATA_PATH search root). Without this guard, addDirectory
	// fires a warning per shader load on emscripten.
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

// Load and cache one of the compat preamble files
// (engines/hpl1/engine/impl/shaders/hpl1_gles2compat.{fragment,vertex}). The
// content is identical across all shaders that use it, so read it once.
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

// The "common" preamble lives in hpl1_gles2compat.fragment but is prepended
// to both fragment and vertex shaders — its sampler/texture polyfills are
// fragment-shaped but harmless in vertex code, and the engine's bundling
// conventions only handle .fragment/.vertex extensions.
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

	// Use loadFromStrings instead of fromStrings so we can survive a failed
	// shader compile (fromStrings calls error() which aborts). When the
	// shader fails to build, the material that uses it will render nothing
	// rather than taking the whole engine down.
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
	// Also set the named-sampler uniforms used by the simple/gamma shaders.
	// Harmless on shaders that don't declare them — setUniform silently
	// ignores missing names.
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
	_shader->use();
#if USE_FORCED_GLES2
	// Keep the framebuffer-size uniform in sync. Set on every Bind() so
	// that screen resize (e.g. browser canvas reflow) takes effect on the
	// next render pass. Shaders that don't declare _hpl1_invFramebufferSize
	// just silently drop this setUniform call.
	const float invW = s_framebufferWidth  > 0 ? 1.0f / (float)s_framebufferWidth  : 0.0f;
	const float invH = s_framebufferHeight > 0 ? 1.0f / (float)s_framebufferHeight : 0.0f;
	_shader->setUniform("_hpl1_invFramebufferSize", {invW, invH});
	// Let the renderer know who's live so DrawQuad's "no shader bound"
	// fallback and UploadShaderMatrix target the right program.
	if (s_pLowLevel)
		s_pLowLevel->NotifyShaderBound(this);
#endif
}

//-----------------------------------------------------------------------

void cCGProgram::UnBind() {
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
	if (mType != eGpuProgramMatrix_ViewProjection) {
		error("unsupported shader matrix %d", mType);
		return false;
	}

	if (mOp != eGpuProgramMatrixOp_Identity) {
		error("unsupported shader matrix operation %d", mOp);
		return false;
	}

#if USE_FORCED_GLES2
	// Desktop GL reads `worldViewProj` from the modelview * projection stacks
	// via glGetFloatv. GLES2 has no fixed-function matrices, so read directly
	// from cLowLevelGraphicsGLES's software matrix stack — the engine pushes
	// the per-object world transform onto eMatrix_ModelView before this call,
	// so the result is World * View * Projection. Without this the world
	// transform is lost and every object collapses to one degenerate triangle.
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
	if (s_pCurrentCamera == nullptr) {
		Math::Matrix4 identity;
		identity.setToIdentity();
		_shader->setUniform(asName.c_str(), identity);
		return true;
	}
	const cMatrixf &viewMatrix = s_pCurrentCamera->GetViewMatrix();
	const cMatrixf &projMatrix = s_pCurrentCamera->GetProjectionMatrix();
	cMatrixf viewProjMatrix = cMath::MatrixMul(projMatrix, viewMatrix);
	Math::Matrix4 mat4;
	mat4.setData(viewProjMatrix.v);
	mat4.transpose();
	_shader->setUniform(asName.c_str(), mat4);
#endif

	return true;
}

} // namespace hpl

#endif // HPL1_USE_OPENGL
