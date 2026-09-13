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

#include "hpl1/engine/impl/LowLevelGraphicsGLES.h"

#include "hpl1/engine/graphics/bitmap2D.h"
#include "hpl1/engine/graphics/font_data.h"
#include "hpl1/engine/impl/CGProgram.h"
#include "hpl1/engine/impl/SDLTexture.h"
#include "hpl1/engine/impl/VertexBufferOGL.h"
#include "hpl1/engine/impl/VertexBufferVBO.h"
#include "hpl1/engine/math/Math.h"
#include "hpl1/engine/system/low_level_system.h"

#include "common/algorithm.h"
#include "common/array.h"
#include "common/config-manager.h"
#include "common/system.h"
#include "engines/util.h"
#include "graphics/cursorman.h"
#include "hpl1/debug.h"
#include "hpl1/graphics.h"

#ifdef HPL1_USE_OPENGL
#if USE_FORCED_GLES2

namespace hpl {

GLenum TextureTargetToGL(eTextureTarget target) {
	switch (target) {
	case eTextureTarget_1D:
		return GL_TEXTURE_2D;
	case eTextureTarget_2D:
		return GL_TEXTURE_2D;
	case eTextureTarget_CubeMap:
		return GL_TEXTURE_CUBE_MAP;
	case eTextureTarget_3D:
		return GL_TEXTURE_3D;
	default:
		break;
	}
	Hpl1::logError(Hpl1::kDebugOpenGL, "invalid texture target (%d)\n", target);
	return GL_TEXTURE_2D;
}

cLowLevelGraphicsGLES::cLowLevelGraphicsGLES() {
	mlBatchArraySize = 20000;
	mlVertexCount = 0;
	mlIndexCount = 0;
	mlMultisampling = 0;
	mvVirtualSize.x = 800;
	mvVirtualSize.y = 600;
	mfGammaCorrection = 1.0;
	mpRenderTarget = nullptr;
	_screenBuffer = nullptr;
	_gammaCorrectionProgram = nullptr;
	mpPixelFormat = Graphics::PixelFormat::createFormatRGBA32();

	Common::fill(mpCurrentTexture, mpCurrentTexture + MAX_TEXTUREUNITS, nullptr);

	// Software matrix stack initial state.
	for (int i = 0; i < eMatrix_LastEnum; ++i)
		mMatrixStack[i].push(cMatrixf::Identity);

	mbClearColor = true;
	mbClearDepth = true;
	mbClearStencil = false;

	mlBatchStride = 13;
	// 3 Pos floats, 4 color floats, 3 Tex coord floats .
	mpVertexArray = (float *)hplMalloc(sizeof(float) * mlBatchStride * mlBatchArraySize);
	mpIndexArray = (unsigned int *)hplMalloc(sizeof(unsigned int) * mlBatchArraySize); // Index is one int.

	for (int i = 0; i < MAX_TEXTUREUNITS; i++) {
		mpTexCoordArray[i] = (float *)hplMalloc(sizeof(float) * 3 * mlBatchArraySize);
		mbTexCoordArrayActive[i] = false;
		mlTexCoordArrayCount[i] = 0;
	}
}

cLowLevelGraphicsGLES::~cLowLevelGraphicsGLES() {
	// SDL_SetGammaRamp(mvStartGammaArray[0],mvStartGammaArray[1],mvStartGammaArray[2]);

	hplFree(mpVertexArray);
	hplFree(mpIndexArray);
	for (int i = 0; i < MAX_TEXTUREUNITS; i++)
		hplFree(mpTexCoordArray[i]);
	hplDelete(_gammaCorrectionProgram);
	hplDelete(_screenBuffer);
}

bool cLowLevelGraphicsGLES::Init(int alWidth, int alHeight, int alBpp, int abFullscreen,
								int alMultisampling, const tString &asWindowCaption) {
	if (abFullscreen) {
		int viewportSize[4];
		GL_CHECK(glGetIntegerv(GL_VIEWPORT, viewportSize));
		mvScreenSize.x = viewportSize[2];
		mvScreenSize.y = viewportSize[3];
	} else {
		mvScreenSize.x = alWidth;
		mvScreenSize.y = alHeight;
	}
	mlBpp = alBpp;
	mlMultisampling = alMultisampling;
	initGraphics3d(mvScreenSize.x, mvScreenSize.y);
	SetupGL();
	ShowCursor(false);
	// CheckMultisampleCaps();
	g_system->updateScreen();

	// Inform cCGProgram of the framebuffer size so the texture2DRect()
	// polyfill can normalize coords. Must happen before any shader compile.
	cCGProgram::SetCurrentFramebufferSize(mvScreenSize.x, mvScreenSize.y);
	// Also wire up the matrix-stack source so SetMatrixf(ViewProjection)
	// can read modelview/projection (replaces glGetFloatv on legacy GL).
	cCGProgram::SetCurrentLowLevel(this);
	// GLES2 doesn't have a fixed-function pipeline; bind a simple shader and a
	// 1x1 default texture so the rest of the engine can keep treating us like
	// the desktop GL renderer.
	mSimpleShader = CreateGpuProgram("hpl1_Simple", "hpl1_Simple");
	if (mSimpleShader)
		mSimpleShader->Bind();
	mDefaultTexture = CreateTexture(cVector2l(1, 1), 32, cColor(1, 1), false,
									eTextureType_Normal, eTextureTarget_2D);
	// Gamma correction post-pass: capture the framebuffer to _screenBuffer
	// before the SwapBuffers, then draw a full-screen quad through the
	// gamma shader. Same flow as desktop GL — applyGammaCorrection() below
	// does the round-trip.
	_gammaCorrectionProgram = CreateGpuProgram("hpl1_gamma_correction", "hpl1_gamma_correction");
	_screenBuffer = CreateTexture(cVector2l((int)mvScreenSize.x, (int)mvScreenSize.y),
								  32, cColor(0, 0, 0, 0), false,
								  eTextureType_Normal, eTextureTarget_Rect);

	return true;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::CheckMultisampleCaps() {
}

//-----------------------------------------------------------------------

static void logOGLInfo(const cLowLevelGraphicsGLES &graphics) {
	Hpl1::logInfo(Hpl1::kDebugOpenGL, "Max texture image units: %d\n",
				  graphics.GetCaps(eGraphicCaps_MaxTextureImageUnits));
	Hpl1::logInfo(Hpl1::kDebugOpenGL, "Max texture coord units: %d\n",
				  graphics.GetCaps(eGraphicCaps_MaxTextureCoordUnits));
	Hpl1::logInfo(Hpl1::kDebugOpenGL, "Two sided stencil: %d\n",
				  graphics.GetCaps(eGraphicCaps_TwoSideStencil));
	Hpl1::logInfo(Hpl1::kDebugOpenGL, "Vertex Buffer Object: %d\n",
				  graphics.GetCaps(eGraphicCaps_VertexBufferObject));
	Hpl1::logInfo(Hpl1::kDebugOpenGL, "Anisotropic filtering: %d\n",
				  graphics.GetCaps(eGraphicCaps_AnisotropicFiltering));
	Hpl1::logInfo(Hpl1::kDebugOpenGL, "Max Anisotropic degree: %d\n",
				  graphics.GetCaps(eGraphicCaps_MaxAnisotropicFiltering));
	Hpl1::logInfo(Hpl1::kDebugOpenGL, "Multisampling: %d\n",
				  graphics.GetCaps(eGraphicCaps_Multisampling));
}

void cLowLevelGraphicsGLES::SetupGL() {
	GL_CHECK(glViewport(0, 0, mvScreenSize.x, mvScreenSize.y));
	GL_CHECK(glClearColor(0.0f, 0.0f, 0.0f, 0.0f));

	GL_CHECK(glClearDepthf(1.0f));
	GL_CHECK(glEnable(GL_DEPTH_TEST));
	GL_CHECK(glDepthFunc(GL_LEQUAL));

	GL_CHECK(glClearStencil(0));

	GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT));


	logOGLInfo(*this);
}
//-----------------------------------------------------------------------

int cLowLevelGraphicsGLES::GetCaps(eGraphicCaps type) const {
	switch (type) {

	// Texture Rectangle
	case eGraphicCaps_TextureTargetRectangle:
		// WebGL2 has no GL_TEXTURE_RECTANGLE, but on USE_FORCED_GLES2 the
		// renderer remaps Rect→2D in GetGLTextureTargetEnum and the shader
		// preamble polyfills texture2DRect() to a normalized-UV texture2D()
		// sample via _hpl1_invFramebufferSize. The post-effects pipeline
		// (refraction, gamma, screen capture) is treated as available.
		return 1;

	// Vertex Buffer Object
	case eGraphicCaps_VertexBufferObject:
		return 1; // gl 2.0

	// Two Sided Stencil
	case eGraphicCaps_TwoSideStencil:
		return 1; // gl 2.0

	// Max Texture Image Units
	case eGraphicCaps_MaxTextureImageUnits: {
		int lUnits;
		GL_CHECK(glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, (GLint *)&lUnits));
		return lUnits;
	}
	// Max Texture Coord Units
	case eGraphicCaps_MaxTextureCoordUnits: {
		int lUnits = 0;
		// GLES2 has no GL_MAX_TEXTURE_COORDS; the closest analogue is the
		// per-program vertex attribute limit.
		GL_CHECK(glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, (GLint *)&lUnits));
		return lUnits;
	}
	// Texture Anisotropy
	case eGraphicCaps_AnisotropicFiltering:
		return 0; // gl 4.6

	// Texture Anisotropy
	case eGraphicCaps_MaxAnisotropicFiltering:
		return 0; // gl 4.6

	// Multisampling
	case eGraphicCaps_Multisampling:
		return 1; // gl 1.3

	// GL shaders
	case eGraphicCaps_GL_GpuPrograms:
		return Hpl1::areShadersAvailable(); // gl 2.0

	case eGraphicCaps_GL_BlendFunctionSeparate:
		return 1; // gl 1.4

	case eGraphicCaps_GL_MultiTexture:
		return 1; // gl 1.2.1

	default:
		break;
	}
	Hpl1::logWarning(Hpl1::kDebugGraphics, "graphic options %d is not supported\n", type);
	return 0;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::ShowCursor(bool toggle) {
	CursorMan.showMouse(toggle);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetMultisamplingActive(bool toggle) {
	// TODO: AA is locked at WebGL2 context creation; runtime toggle would need a context recreate.
	static bool warned = false;
	if (!warned) { warned = true; Hpl1::logWarning(Hpl1::kDebugOpenGL, "SetMultisamplingActive called on GLES2 (no-op)\n"); }
	(void)toggle;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetGammaCorrection(float afX) {
	mfGammaCorrection = afX;
}

float cLowLevelGraphicsGLES::GetGammaCorrection() {
	return mfGammaCorrection;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetClipPlane(int alIdx, const cPlanef &aPlane) {
	// TODO: emulate via a vertex-shader varying + fragment discard (needed for water reflection).
	static bool warned = false;
	if (!warned) { warned = true; Hpl1::logWarning(Hpl1::kDebugOpenGL, "SetClipPlane called on GLES2 (no-op)\n"); }
	(void)alIdx;
	(void)aPlane;
}

cPlanef cLowLevelGraphicsGLES::GetClipPlane(int alIdx, const cPlanef &aPlane) {
	(void)alIdx;
	(void)aPlane;
	return cPlanef();
}

void cLowLevelGraphicsGLES::SetClipPlaneActive(int alIdx, bool toggle) {
	// TODO: companion to SetClipPlane — needs the same shader-emulation work.
	static bool warned = false;
	if (!warned) { warned = true; Hpl1::logWarning(Hpl1::kDebugOpenGL, "SetClipPlaneActive called on GLES2 (no-op)\n"); }
	(void)alIdx;
	(void)toggle;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SaveScreenToBMP(const tString &asFile) {
	GL_CHECK(glFinish());
	g_system->saveScreenshot();
}

//-----------------------------------------------------------------------

Bitmap2D *cLowLevelGraphicsGLES::CreateBitmap2D(const cVector2l &size) {
	return hplNew(Bitmap2D, (size, mpPixelFormat));
}

//-----------------------------------------------------------------------

FontData *cLowLevelGraphicsGLES::CreateFontData(const tString &asName) {
	return hplNew(FontData, (asName, this));
}

//-----------------------------------------------------------------------

iGpuProgram *cLowLevelGraphicsGLES::CreateGpuProgram(const tString &vertex, const tString &fragment) {
	return hplNew(cCGProgram, (vertex, fragment));
}

//-----------------------------------------------------------------------

Graphics::PixelFormat *cLowLevelGraphicsGLES::GetPixelFormat() {
	return &mpPixelFormat;
}

//-----------------------------------------------------------------------

iTexture *cLowLevelGraphicsGLES::CreateTexture(bool abUseMipMaps, eTextureType aType, eTextureTarget aTarget) {
	return hplNew(cSDLTexture, ("", &mpPixelFormat, this, aType, abUseMipMaps, aTarget));
}

//-----------------------------------------------------------------------

iTexture *cLowLevelGraphicsGLES::CreateTexture(const tString &asName, bool abUseMipMaps, eTextureType aType, eTextureTarget aTarget) {
	return hplNew(cSDLTexture, (asName, &mpPixelFormat, this, aType, abUseMipMaps, aTarget));
}

//-----------------------------------------------------------------------

iTexture *cLowLevelGraphicsGLES::CreateTexture(Bitmap2D *apBmp, bool abUseMipMaps, eTextureType aType,
											  eTextureTarget aTarget) {
	cSDLTexture *pTex = hplNew(cSDLTexture, ("", &mpPixelFormat, this, aType, abUseMipMaps, aTarget));
	pTex->CreateFromBitmap(apBmp);

	return pTex;
}

//-----------------------------------------------------------------------

iTexture *cLowLevelGraphicsGLES::CreateTexture(const cVector2l &avSize, int alBpp, cColor aFillCol,
											  bool abUseMipMaps, eTextureType aType, eTextureTarget aTarget) {
	cSDLTexture *pTex = NULL;

	if (aType == eTextureType_RenderTarget) {
		pTex = hplNew(cSDLTexture, ("", &mpPixelFormat, this, aType, abUseMipMaps, aTarget));
		pTex->Create(avSize.x, avSize.y, aFillCol);
	} else {
		Bitmap2D *pBmp = CreateBitmap2D(avSize);
		pBmp->fillRect(cRect2l(0, 0, 0, 0), aFillCol);

		pTex = hplNew(cSDLTexture, ("", &mpPixelFormat, this, aType, abUseMipMaps, aTarget));
		bool bRet = pTex->CreateFromBitmap(pBmp);

		hplDelete(pBmp);

		if (bRet == false) {
			hplDelete(pTex);
			return NULL;
		}
	}
	return pTex;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::PushMatrix(eMatrix aMtxType) {
	mMatrixStack[aMtxType].push(mMatrixStack[aMtxType].top());
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::PopMatrix(eMatrix aMtxType) {
	mMatrixStack[aMtxType].pop();
}
//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetMatrix(eMatrix aMtxType, const cMatrixf &a_mtxA) {
	mMatrixStack[aMtxType].top() = a_mtxA;
	UploadShaderMatrix();
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetIdentityMatrix(eMatrix aMtxType) {
	mMatrixStack[aMtxType].top() = cMatrixf::Identity;
	UploadShaderMatrix();
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::TranslateMatrix(eMatrix aMtxType, const cVector3f &avPos) {
	cMatrixf &r = mMatrixStack[aMtxType].top();
	cMatrixf m = r;
	for (int i = 0; i < 4; i++)
		r.m[i][3] = m.m[i][0] * avPos.x + m.m[i][1] * avPos.y +
					m.m[i][2] * avPos.z + m.m[i][3];
}

//-----------------------------------------------------------------------

/**
 * \todo fix so that there are X, Y , Z versions of this one.
 * \param aMtxType
 * \param &avRot
 */
void cLowLevelGraphicsGLES::RotateMatrix(eMatrix aMtxType, const cVector3f &avRot) {
	// Multiply the current top of the matrix stack by a rotation through 1°
	// around the (x, y, z) axis, mirroring desktop GL's glRotatef(1, x, y, z).
	// The engine doesn't actively call this on the GLES2 path, but
	// implementing it keeps the stack consistent if something ever does.
	cMatrixf &top = mMatrixStack[aMtxType].top();
	const cMatrixf rot = cMath::MatrixRotate(cVector3f(avRot.x, avRot.y, avRot.z) * cMath::ToRad(1.0f),
											 eEulerRotationOrder_XYZ);
	top = cMath::MatrixMul(top, rot);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::ScaleMatrix(eMatrix aMtxType, const cVector3f &avScale) {
	VEC3_CONST_ARRAY(vel, avScale);
	cMatrixf &r = mMatrixStack[aMtxType].top();
	cMatrixf m = r;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 4; j++)
			r.m[j][i] = m.m[j][i] * vel[i];
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetOrthoProjection(const cVector2f &avSize, float afMin, float afMax) {
	cMatrixf &r = mMatrixStack[eMatrix_Projection].top();
	r = cMatrixf::Identity;
	r.m[0][0] = 2.0f / avSize.x;
	r.m[1][1] = 2.0f / -avSize.y;
	r.m[2][2] = -2.0f / (afMax - afMin);
	r.m[0][3] = -1.0f;
	r.m[1][3] = 1.0f;
	r.m[2][3] = -(afMax + afMin) / (afMax - afMin);
	r.m[3][3] = 1;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetTexture(unsigned int alUnit, iTexture *apTex) {
	// GLES2 has no fixed-function "texture unit enable"; emulate "no texture"
	// by binding a default 1x1 white texture so shaders sample something sane.
	if (apTex == nullptr)
		apTex = mDefaultTexture;

	if (apTex == mpCurrentTexture[alUnit])
		return;

	GLenum NewTarget = 0;
	if (apTex)
		NewTarget = GetGLTextureTargetEnum(apTex->GetTarget());
	GLenum LastTarget = 0;
	if (mpCurrentTexture[alUnit])
		LastTarget = GetGLTextureTargetEnum(mpCurrentTexture[alUnit]->GetTarget());

	// GLES2 path: just rebind. No glEnable(target) (INVALID_ENUM in GLES2),
	// no LastTarget unbind dance — sampling is purely a shader concern.
	(void)LastTarget;
	GL_CHECK(glActiveTexture(GL_TEXTURE0 + alUnit));
	cSDLTexture *pSDLTex = static_cast<cSDLTexture *>(apTex);
	GL_CHECK(glBindTexture(NewTarget, pSDLTex->GetTextureHandle()));

	mpCurrentTexture[alUnit] = apTex;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetActiveTextureUnit(unsigned int alUnit) {
	GL_CHECK(glActiveTexture(GL_TEXTURE0 + alUnit));
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetTextureEnv(eTextureParam aParam, int alVal) {
	// Emulate the subset of combiner state that callers actually use: the
	// AlphaSource0=Constant + AlphaFunc=Replace pair (image-trail crossfade,
	// PostEffects motion blur) ends up driving _hpl1_alphaOverride on
	// mSimpleShader. Other combiner parameters are no-ops; if a future
	// material needs e.g. ColorSource combinations, extend here.
	if (aParam == eTextureParam_AlphaSource0)
		mAlphaUsesConstant = (alVal == eTextureSource_Constant);
	else if (aParam == eTextureParam_AlphaFunc)
		mAlphaOverride = (alVal == eTextureFunc_Replace && mAlphaUsesConstant) ? 1.0f : 0.0f;
	if (mpActiveShader)
		mpActiveShader->SetFloat("_hpl1_alphaOverride", mAlphaOverride);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetTextureConstantColor(const cColor &color) {
	mConstantColor[0] = color.r;
	mConstantColor[1] = color.g;
	mConstantColor[2] = color.b;
	mConstantColor[3] = color.a;
	if (mpActiveShader)
		mpActiveShader->SetVec4f("_hpl1_constantColor",
								 mConstantColor[0], mConstantColor[1],
								 mConstantColor[2], mConstantColor[3]);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetColor(const cColor &aColor) {
	// TODO: migrate callers to per-vertex attributes or a uniform if any start relying on it.
	static bool warned = false;
	if (!warned) { warned = true; Hpl1::logWarning(Hpl1::kDebugOpenGL, "SetColor called on GLES2 (no-op)\n"); }
	(void)aColor;
}

//-----------------------------------------------------------------------

iVertexBuffer *cLowLevelGraphicsGLES::CreateVertexBuffer(tVertexFlag aFlags,
														eVertexBufferDrawType aDrawType, eVertexBufferUsageType aUsageType, int alReserveVtxSize, int alReserveIdxSize) {

	if (GetCaps(eGraphicCaps_VertexBufferObject))
		return hplNew(cVertexBufferVBO, (this, aFlags, aDrawType, aUsageType, alReserveVtxSize, alReserveIdxSize));
	// cVertexBufferOGL is a fixed-function fallback (glVertexPointer / glClientActiveTexture)
	// that doesn't exist in GLES2. We always have VBO support here anyway.
	return hplNew(cVertexBufferVBO, (this, aFlags, aDrawType, aUsageType, alReserveVtxSize, alReserveIdxSize));
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::DrawRect(const cVector2f &avPos, const cVector2f &avSize, float afZ) {
	// Textured 2D rect — two triangles with UVs (0..1, 0..1) so the bound
	// shader's tex0 sample lines up with the legacy glTexCoord2f path.
	const float pos[] = {
		avPos.x,            avPos.y,            afZ,
		avPos.x + avSize.x, avPos.y,            afZ,
		avPos.x + avSize.x, avPos.y + avSize.y, afZ,
		avPos.x,            avPos.y,            afZ,
		avPos.x + avSize.x, avPos.y + avSize.y, afZ,
		avPos.x,            avPos.y + avSize.y, afZ,
	};
	const float uvs[] = {
		0.f, 0.f, 0.f,  1.f, 0.f, 0.f,  1.f, 1.f, 0.f,
		0.f, 0.f, 0.f,  1.f, 1.f, 0.f,  0.f, 1.f, 0.f,
	};
	if (!mpActiveShader && mSimpleShader)
		mSimpleShader->Bind();
	UploadShaderMatrix();
	glEnableVertexAttribArray(eVtxAttr_Position);
	glEnableVertexAttribArray(eVtxAttr_Texture0);
	glDisableVertexAttribArray(eVtxAttr_Color0);
	glDisableVertexAttribArray(eVtxAttr_Normal);
	glDisableVertexAttribArray(eVtxAttr_Tangent);
	glVertexAttrib4f(eVtxAttr_Color0, 1, 1, 1, 1);
	glVertexAttribPointer(eVtxAttr_Position, 3, GL_FLOAT, false, 0, pos);
	glVertexAttribPointer(eVtxAttr_Texture0, 3, GL_FLOAT, false, 0, uvs);
	GL_CHECK(glDrawArrays(GL_TRIANGLES, 0, 6));
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::FlushRendering() {
	GL_CHECK(glFlush());
}

void cLowLevelGraphicsGLES::applyGammaCorrection() {
	if (!_gammaCorrectionProgram || !_screenBuffer)
		return;
	// User has gamma == 1.0 effectively means "don't bother"; skip the
	// round-trip to save the per-frame screen capture + composite.
	if (mfGammaCorrection == 1.0f)
		return;

	SetBlendActive(false);

	CopyContextToTexure(_screenBuffer, 0,
						cVector2l((int)mvScreenSize.x, (int)mvScreenSize.y));

	// Full-screen quad in clip space (the gamma vertex shader writes
	// gl_Position = a_position directly, no worldViewProj). Texcoords
	// differ between paths: desktop _screenBuffer is GL_TEXTURE_RECTANGLE
	// (pixel-space), GLES2 remaps it to GL_TEXTURE_2D (normalized).
	tVertexVec vVtx;
	vVtx.push_back(cVertex(cVector3f(-1.0, 1.0, 0), cVector2f(0, 1), cColor(0)));
	vVtx.push_back(cVertex(cVector3f(1.0, 1.0, 0), cVector2f(1, 1), cColor(0)));
	vVtx.push_back(cVertex(cVector3f(1.0, -1.0, 0), cVector2f(1, 0), cColor(0)));
	vVtx.push_back(cVertex(cVector3f(-1.0, -1.0, 0), cVector2f(0, 0), cColor(0)));

	_gammaCorrectionProgram->Bind();
	SetTexture(0, _screenBuffer);
	_gammaCorrectionProgram->SetFloat("gamma", mfGammaCorrection);
	DrawQuad(vVtx);
	_gammaCorrectionProgram->UnBind();
}

void cLowLevelGraphicsGLES::SwapBuffers() {
	applyGammaCorrection();
	GL_CHECK(glFlush());
	g_system->updateScreen();
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::DrawTri(const tVertexVec &avVtx) {
	assert(avVtx.size() == 3);
	if (!mpActiveShader && mSimpleShader)
		mSimpleShader->Bind();
	UploadShaderMatrix();
	glEnableVertexAttribArray(eVtxAttr_Position);
	glEnableVertexAttribArray(eVtxAttr_Color0);
	glEnableVertexAttribArray(eVtxAttr_Texture0);
	glDisableVertexAttribArray(eVtxAttr_Normal);
	glDisableVertexAttribArray(eVtxAttr_Tangent);
	glVertexAttribPointer(eVtxAttr_Position, 3, GL_FLOAT, false, sizeof(cVertex), &avVtx[0].pos.x);
	glVertexAttribPointer(eVtxAttr_Color0, 4, GL_FLOAT, false, sizeof(cVertex), &avVtx[0].col.r);
	glVertexAttribPointer(eVtxAttr_Texture0, 3, GL_FLOAT, false, sizeof(cVertex), &avVtx[0].tex.x);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::DrawTri(const cVertex *avVtx) {
	if (!mpActiveShader && mSimpleShader)
		mSimpleShader->Bind();
	UploadShaderMatrix();
	glEnableVertexAttribArray(eVtxAttr_Position);
	glEnableVertexAttribArray(eVtxAttr_Color0);
	glEnableVertexAttribArray(eVtxAttr_Texture0);
	glDisableVertexAttribArray(eVtxAttr_Normal);
	glDisableVertexAttribArray(eVtxAttr_Tangent);
	glVertexAttribPointer(eVtxAttr_Position, 3, GL_FLOAT, false, sizeof(cVertex), &avVtx[0].pos.x);
	glVertexAttribPointer(eVtxAttr_Color0, 4, GL_FLOAT, false, sizeof(cVertex), &avVtx[0].col.r);
	glVertexAttribPointer(eVtxAttr_Texture0, 3, GL_FLOAT, false, sizeof(cVertex), &avVtx[0].tex.x);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::DrawPrimitiveLines(unsigned int aMode, const float *positions,
											  int aVertexCount, const cColor &aColor) {
	if (!mpActiveShader && mSimpleShader)
		mSimpleShader->Bind();
	UploadShaderMatrix();
	glEnableVertexAttribArray(eVtxAttr_Position);
	glDisableVertexAttribArray(eVtxAttr_Color0);
	glDisableVertexAttribArray(eVtxAttr_Texture0);
	glDisableVertexAttribArray(eVtxAttr_Normal);
	glDisableVertexAttribArray(eVtxAttr_Tangent);
	// Set a constant per-vertex color via the disabled-attribute fallback —
	// avoids uploading a parallel color buffer for line debug overlays.
	glVertexAttrib4f(eVtxAttr_Color0, aColor.r, aColor.g, aColor.b, aColor.a);
	glVertexAttribPointer(eVtxAttr_Position, 3, GL_FLOAT, false, 0, positions);
	GL_CHECK(glDrawArrays(aMode, 0, aVertexCount));
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::DrawQuad(const tVertexVec &avVtx) {
	assert(avVtx.size() == 4);

	// Emulate the immediate-mode quad with two indexed triangles drawn via
	// the simple shader's generic attributes. If a caller (e.g. post-effects
	// blur) already bound its own shader, leave it alone — clobbering here
	// would replace the caller's program with the simple pass-through and
	// silently break the effect.
	if (!mpActiveShader && mSimpleShader)
		mSimpleShader->Bind();
	UploadShaderMatrix();

	glEnableVertexAttribArray(eVtxAttr_Position);
	glEnableVertexAttribArray(eVtxAttr_Color0);
	glEnableVertexAttribArray(eVtxAttr_Texture0);
	glDisableVertexAttribArray(eVtxAttr_Normal);
	glDisableVertexAttribArray(eVtxAttr_Tangent);

	glVertexAttribPointer(eVtxAttr_Position, 3, GL_FLOAT, false, sizeof(cVertex), &avVtx[0].pos.x);
	glVertexAttribPointer(eVtxAttr_Color0, 4, GL_FLOAT, false, sizeof(cVertex), &avVtx[0].col.r);
	glVertexAttribPointer(eVtxAttr_Texture0, 3, GL_FLOAT, false, sizeof(cVertex), &avVtx[0].tex.x);

	const uint16_t quadIndices[] = {0, 1, 2, 2, 3, 0};
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, quadIndices);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::DrawQuadMultiTex(const tVertexVec &avVtx, const tVector3fVec &avExtraUvs) {
	assert(avVtx.size() == 4);
	// GLES2 path: drive the currently-bound shader (bloom uses 2 texcoord
	// streams via gl_MultiTexCoord0 / gl_MultiTexCoord1, aliased in the
	// HPL1 compat preamble to _hpl1_uv (slot eVtxAttr_Texture0) and
	// _hpl1_tangent (slot eVtxAttr_Tangent)).
	//
	// RenderDepthOfField calls this without a bound program (it relies on
	// fixed-function combiners on desktop). Fall back to mSimpleShader so
	// the draw is valid on GLES2 — the combiner state is a no-op anyway.
	if (!mpActiveShader && mSimpleShader)
		mSimpleShader->Bind();
	UploadShaderMatrix();
	glEnableVertexAttribArray(eVtxAttr_Position);
	glEnableVertexAttribArray(eVtxAttr_Color0);
	glEnableVertexAttribArray(eVtxAttr_Texture0);
	glDisableVertexAttribArray(eVtxAttr_Normal);
	glVertexAttribPointer(eVtxAttr_Position, 3, GL_FLOAT, false, sizeof(cVertex), &avVtx[0].pos.x);
	glVertexAttribPointer(eVtxAttr_Color0, 4, GL_FLOAT, false, sizeof(cVertex), &avVtx[0].col.r);
	glVertexAttribPointer(eVtxAttr_Texture0, 3, GL_FLOAT, false, sizeof(cVertex), &avVtx[0].tex.x);

	const int lExtraUnits = (int)avExtraUvs.size() / 4;
	if (lExtraUnits >= 1) {
		glEnableVertexAttribArray(eVtxAttr_Tangent);
		glVertexAttribPointer(eVtxAttr_Tangent, 3, GL_FLOAT, false, sizeof(cVector3f), &avExtraUvs[0].x);
	} else {
		glDisableVertexAttribArray(eVtxAttr_Tangent);
	}

	const uint16_t quadIndices[] = {0, 1, 2, 2, 3, 0};
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, quadIndices);
}

//-----------------------------------------------------------------------

iOcclusionQuery *cLowLevelGraphicsGLES::CreateOcclusionQuery() {
	// WebGL2 only exposes ANY_SAMPLES_PASSED (boolean), and the depth-tested
	// flavor reads as occluded in the cabin because alpha-tested wall
	// fragments still write depth on most browsers. Skip the query entirely;
	// cBillboard::UpdateGraphics treats a null mpQuery as "always visible".
	// TODO: N-tile subdivision could recover fractional dimming if needed.
	return nullptr;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::DestroyOcclusionQuery(iOcclusionQuery *apQuery) {
	if (apQuery)
		hplDelete(apQuery);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::ClearScreen() {
	GLbitfield bitmask = 0;

	if (mbClearColor)
		bitmask |= GL_COLOR_BUFFER_BIT;
	if (mbClearDepth)
		bitmask |= GL_DEPTH_BUFFER_BIT;
	if (mbClearStencil)
		bitmask |= GL_STENCIL_BUFFER_BIT;

	GL_CHECK(glClear(bitmask));
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetClearColor(const cColor &aCol) {
	GL_CHECK(glClearColor(aCol.r, aCol.g, aCol.b, aCol.a));
}
void cLowLevelGraphicsGLES::SetClearDepth(float afDepth) {
	GL_CHECK(glClearDepthf(afDepth));
}
void cLowLevelGraphicsGLES::SetClearStencil(int alVal) {
	GL_CHECK(glClearStencil(alVal));
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetClearColorActive(bool abX) {
	mbClearColor = abX;
}
void cLowLevelGraphicsGLES::SetClearDepthActive(bool abX) {
	mbClearDepth = abX;
}
void cLowLevelGraphicsGLES::SetClearStencilActive(bool abX) {
	mbClearStencil = abX;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetColorWriteActive(bool abR, bool abG, bool abB, bool abA) {
	glColorMask(abR, abG, abB, abA);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetDepthWriteActive(bool abX) {
	glDepthMask(abX);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetDepthTestActive(bool abX) {
	if (abX)
		glEnable(GL_DEPTH_TEST);
	else
		glDisable(GL_DEPTH_TEST);
	GL_CHECK_FN();
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetDepthTestFunc(eDepthTestFunc aFunc) {
	GL_CHECK(glDepthFunc(GetGLDepthTestFuncEnum(aFunc)));
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetAlphaTestActive(bool abX) {
	// GLES2 has no GL_ALPHA_TEST; the fragment compat preamble declares
	// `uniform float _hpl1_alphaRef` and injectAlphaTest appends a
	// `discard` against it at the end of main(). Disabling the test means
	// pushing 0 so the predicate `outColor.a < 0` never fires. Cached so
	// it survives a Bind() that happens after this call (the engine sets
	// alpha state and binds the program in either order).
	if (!abX) {
		mAlphaTestRef = 0.0f;
		if (mpActiveShader)
			mpActiveShader->SetFloat("_hpl1_alphaRef", 0.0f);
	}
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetAlphaTestFunc(eAlphaTestFunc aFunc, float afRef) {
	// The engine only asks for GreaterOrEqual today (Renderer3D); the
	// shader-injected check `outColor.a < _hpl1_alphaRef → discard` is
	// equivalent. Cache so a later Bind() picks it up.
	(void)aFunc;
	mAlphaTestRef = afRef;
	if (mpActiveShader)
		mpActiveShader->SetFloat("_hpl1_alphaRef", afRef);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetStencilActive(bool abX) {
	if (abX)
		glEnable(GL_STENCIL_TEST);
	else
		glDisable(GL_STENCIL_TEST);
	GL_CHECK_FN();
}

//-----------------------------------------------------------------------

/*void cLowLevelGraphicsGLES::SetStencilTwoSideActive(bool abX)
{
	if(GLEE_EXT_stencil_two_side)
	{
		glEnable(GL_STENCIL_TEST_TWO_SIDE_EXT);
	}
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetStencilFace(eStencilFace aFace)
{
	if(GLEE_EXT_stencil_two_side)
	{
		if(aFace == eStencilFace_Front) glActiveStencilFaceEXT(GL_FRONT);
		else							glActiveStencilFaceEXT(GL_BACK);
	}
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetStencilFunc(eStencilFunc aFunc,int alRef, unsigned int aMask)
{
	glStencilFunc(GetGLStencilFuncEnum(aFunc), alRef, aMask);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetStencilOp(eStencilOp aFailOp,eStencilOp aZFailOp,eStencilOp aZPassOp)
{
	glStencilOp(GetGLStencilOpEnum(aFailOp), GetGLStencilOpEnum(aZFailOp),
				GetGLStencilOpEnum(aZPassOp));
}*/

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetStencil(eStencilFunc aFunc, int alRef, unsigned int aMask,
									  eStencilOp aFailOp, eStencilOp aZFailOp, eStencilOp aZPassOp) {
#if 0
	if (GetCaps(eGraphicCaps_TwoSideStencil)) {
		//glDisable(GL_STENCIL_TEST_TWO_SIDE_EXT);//shouldn't be needed..
		//glActiveStencilFace(GL_FRONT);
	}
#endif
	GL_CHECK(glStencilFunc(GetGLStencilFuncEnum(aFunc), alRef, aMask));

	GL_CHECK(glStencilOp(GetGLStencilOpEnum(aFailOp), GetGLStencilOpEnum(aZFailOp),
						 GetGLStencilOpEnum(aZPassOp)));
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetStencilTwoSide(eStencilFunc aFrontFunc, eStencilFunc aBackFunc,
											 int alRef, unsigned int aMask, eStencilOp aFrontFailOp, eStencilOp aFrontZFailOp, eStencilOp aFrontZPassOp,
											 eStencilOp aBackFailOp, eStencilOp aBackZFailOp, eStencilOp aBackZPassOp) {
	if (GetCaps(eGraphicCaps_TwoSideStencil)) {
		GL_CHECK(glStencilFuncSeparate(GL_FRONT, GetGLStencilFuncEnum(aFrontFunc), alRef, aMask));
		GL_CHECK(glStencilOpSeparate(GL_FRONT, GetGLStencilOpEnum(aFrontFailOp), GetGLStencilOpEnum(aFrontZFailOp),
									 GetGLStencilOpEnum(aFrontZPassOp)))
		GL_CHECK(glStencilFuncSeparate(GL_BACK, GetGLStencilFuncEnum(aBackFunc), alRef, aMask));
		GL_CHECK(glStencilOpSeparate(GL_BACK, GetGLStencilOpEnum(aBackFailOp), GetGLStencilOpEnum(aBackZFailOp),
									 GetGLStencilOpEnum(aBackZPassOp)));
	} else
		error("Only single sided stencil supported");
}

void cLowLevelGraphicsGLES::SetStencilTwoSide(bool abX) {
	if (!GetCaps(eGraphicCaps_TwoSideStencil))
		Hpl1::logError(Hpl1::kDebugOpenGL, "call to setStencilTwoSide with two side stencil disabled%c\n", '.');
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetCullActive(bool abX) {
	// Penumbra's cabin level (and likely others) contains meshes with
	// inconsistent CCW/CW winding around the porthole geometry. On the
	// native desktop renderer these never visibly diverge, but on the
	// stricter WebGL2 path culling produces a hard pie-wedge clip across
	// the porthole disc. The perf cost of leaving cull off in WebGL is
	// negligible for HPL1's geometry density, so disable it unconditionally
	// here rather than re-tessellating affected meshes.
	glDisable(GL_CULL_FACE);
	GL_CHECK_FN();
	(void)abX;
}
void cLowLevelGraphicsGLES::SetCullMode(eCullMode aMode) {
	GL_CHECK(glCullFace(GL_BACK));
	if (aMode == eCullMode_Clockwise)
		glFrontFace(GL_CCW);
	else
		glFrontFace(GL_CW);
	GL_CHECK_FN();
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetScissorActive(bool toggle) {
	if (toggle)
		glEnable(GL_SCISSOR_TEST);
	else
		glDisable(GL_SCISSOR_TEST);
	GL_CHECK_FN();
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetScissorRect(const cRect2l &aRect) {
	glScissor(aRect.x, (mvScreenSize.y - aRect.y - 1) - aRect.h, aRect.w, aRect.h);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetBlendActive(bool abX) {
	if (abX)
		glEnable(GL_BLEND);
	else
		glDisable(GL_BLEND);
	GL_CHECK_FN();
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetBlendFunc(eBlendFunc aSrcFactor, eBlendFunc aDestFactor) {
	GL_CHECK(glBlendFunc(GetGLBlendEnum(aSrcFactor), GetGLBlendEnum(aDestFactor)));
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetBlendFuncSeparate(eBlendFunc aSrcFactorColor, eBlendFunc aDestFactorColor,
												eBlendFunc aSrcFactorAlpha, eBlendFunc aDestFactorAlpha) {
	if (GetCaps(eGraphicCaps_GL_BlendFunctionSeparate)) {

		glBlendFuncSeparate(GetGLBlendEnum(aSrcFactorColor),
							GetGLBlendEnum(aDestFactorColor),
							GetGLBlendEnum(aSrcFactorAlpha),
							GetGLBlendEnum(aDestFactorAlpha));
	} else {
		glBlendFunc(GetGLBlendEnum(aSrcFactorColor), GetGLBlendEnum(aDestFactorColor));
	}
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::DrawQuad(const tVertexVec &avVtx, const cColor aCol) {
	assert(avVtx.size() == 4);

	(void)avVtx;
	(void)aCol;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::DrawQuad(const tVertexVec &avVtx, const float afZ) {
	assert(avVtx.size() == 4);

	(void)avVtx;
	(void)afZ;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::DrawQuad(const tVertexVec &avVtx, const float afZ, const cColor &aCol) {
	assert(avVtx.size() == 4);

	(void)avVtx;
	(void)afZ;
	(void)aCol;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::AddVertexToBatch(const cVertex &apVtx) {
	// Coord
	mpVertexArray[mlVertexCount + 0] = apVtx.pos.x;
	mpVertexArray[mlVertexCount + 1] = apVtx.pos.y;
	mpVertexArray[mlVertexCount + 2] = apVtx.pos.z;
	// Color
	mpVertexArray[mlVertexCount + 3] = apVtx.col.r;
	mpVertexArray[mlVertexCount + 4] = apVtx.col.g;
	mpVertexArray[mlVertexCount + 5] = apVtx.col.b;
	mpVertexArray[mlVertexCount + 6] = apVtx.col.a;
	// Texture coord
	mpVertexArray[mlVertexCount + 7] = apVtx.tex.x;
	mpVertexArray[mlVertexCount + 8] = apVtx.tex.y;
	mpVertexArray[mlVertexCount + 9] = apVtx.tex.z;
	// Normal coord
	mpVertexArray[mlVertexCount + 10] = apVtx.norm.x;
	mpVertexArray[mlVertexCount + 11] = apVtx.norm.y;
	mpVertexArray[mlVertexCount + 12] = apVtx.norm.z;

	mlVertexCount = mlVertexCount + mlBatchStride;

	if (mlVertexCount / mlBatchStride >= mlBatchArraySize) {
		// Make the array larger.
	}
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::AddVertexToBatch(const cVertex *apVtx, const cVector3f *avTransform) {
	// Coord
	mpVertexArray[mlVertexCount + 0] = apVtx->pos.x + avTransform->x;
	mpVertexArray[mlVertexCount + 1] = apVtx->pos.y + avTransform->y;
	mpVertexArray[mlVertexCount + 2] = apVtx->pos.z + avTransform->z;

	/*Log("Trans: %s\n",avTransform->ToString().c_str());
	Log("Adding: %f:%f:%f\n",mpVertexArray[mlVertexCount + 0],
								mpVertexArray[mlVertexCount + 1],
								mpVertexArray[mlVertexCount + 2]);*/
	// Color
	mpVertexArray[mlVertexCount + 3] = apVtx->col.r;
	mpVertexArray[mlVertexCount + 4] = apVtx->col.g;
	mpVertexArray[mlVertexCount + 5] = apVtx->col.b;
	mpVertexArray[mlVertexCount + 6] = apVtx->col.a;
	// Texture coord
	mpVertexArray[mlVertexCount + 7] = apVtx->tex.x;
	mpVertexArray[mlVertexCount + 8] = apVtx->tex.y;
	mpVertexArray[mlVertexCount + 9] = apVtx->tex.z;

	/*Log("Tex: %f:%f:%f\n",mpVertexArray[mlVertexCount + 7],
		mpVertexArray[mlVertexCount + 8],
		mpVertexArray[mlVertexCount + 9]);*/

	// Normal coord
	mpVertexArray[mlVertexCount + 10] = apVtx->norm.x;
	mpVertexArray[mlVertexCount + 11] = apVtx->norm.y;
	mpVertexArray[mlVertexCount + 12] = apVtx->norm.z;

	mlVertexCount = mlVertexCount + mlBatchStride;

	if (mlVertexCount / mlBatchStride >= mlBatchArraySize) {
		// Make the array larger.
	}
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::AddVertexToBatch(const cVertex *apVtx, const cMatrixf *aMtx) {
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::AddVertexToBatch_Size2D(const cVertex *apVtx, const cVector3f *avTransform,
												   const cColor *apCol, const float &mfW, const float &mfH) {
	// Coord
	mpVertexArray[mlVertexCount + 0] = avTransform->x + mfW;
	mpVertexArray[mlVertexCount + 1] = avTransform->y + mfH;
	mpVertexArray[mlVertexCount + 2] = avTransform->z;

	// Color
	mpVertexArray[mlVertexCount + 3] = apCol->r;
	mpVertexArray[mlVertexCount + 4] = apCol->g;
	mpVertexArray[mlVertexCount + 5] = apCol->b;
	mpVertexArray[mlVertexCount + 6] = apCol->a;

	// Texture coord
	mpVertexArray[mlVertexCount + 7] = apVtx->tex.x;
	mpVertexArray[mlVertexCount + 8] = apVtx->tex.y;
	mpVertexArray[mlVertexCount + 9] = apVtx->tex.z;

	mlVertexCount = mlVertexCount + mlBatchStride;

	if (mlVertexCount / mlBatchStride >= mlBatchArraySize) {
		// Make the array larger.
	}
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::AddVertexToBatch_Raw(const cVector3f &avPos, const cColor &aColor,
												const cVector3f &avTex) {
	// Coord
	mpVertexArray[mlVertexCount + 0] = avPos.x;
	mpVertexArray[mlVertexCount + 1] = avPos.y;
	mpVertexArray[mlVertexCount + 2] = avPos.z;

	// Color
	mpVertexArray[mlVertexCount + 3] = aColor.r;
	mpVertexArray[mlVertexCount + 4] = aColor.g;
	mpVertexArray[mlVertexCount + 5] = aColor.b;
	mpVertexArray[mlVertexCount + 6] = aColor.a;

	// Texture coord
	mpVertexArray[mlVertexCount + 7] = avTex.x;
	mpVertexArray[mlVertexCount + 8] = avTex.y;
	mpVertexArray[mlVertexCount + 9] = avTex.z;

	mlVertexCount = mlVertexCount + mlBatchStride;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::AddIndexToBatch(int alIndex) {
	mpIndexArray[mlIndexCount] = alIndex;
	mlIndexCount++;

	if (mlIndexCount >= mlBatchArraySize) {
		// Make the array larger.
	}
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::AddTexCoordToBatch(unsigned int alUnit, const cVector3f *apCoord) {
	unsigned int lCount = mlTexCoordArrayCount[alUnit];

	mpTexCoordArray[alUnit][lCount + 0] = apCoord->x;
	mpTexCoordArray[alUnit][lCount + 1] = apCoord->y;
	mpTexCoordArray[alUnit][lCount + 2] = apCoord->z;

	mlTexCoordArrayCount[alUnit] += 3;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetBatchTextureUnitActive(unsigned int alUnit, bool active) {
	// No fixed-function client tex-coord pointer in GLES2; texture coord
	// routing is purely via the shader's bound vertex attribute layout.
	(void)alUnit;
	(void)active;
}

//-----------------------------------------------------------------------

static void flushAutoClear(unsigned &indexCount, unsigned &vertexCount, unsigned *texCoordArray) {
	indexCount = 0;
	vertexCount = 0;
	Common::fill(texCoordArray, texCoordArray + MAX_TEXTUREUNITS, 0);
}

void cLowLevelGraphicsGLES::FlushTriBatch(tVtxBatchFlag aTypeFlags, bool abAutoClear) {
	if (mSimpleShader)
		mSimpleShader->Bind();
	UploadShaderMatrix();
	SetVtxBatchStates(aTypeFlags);
	SetUpBatchArrays();
	GL_CHECK(glDrawElements(GL_TRIANGLES, mlIndexCount, GL_UNSIGNED_INT, mpIndexArray));
	if (abAutoClear)
		flushAutoClear(mlIndexCount, mlVertexCount, mlTexCoordArrayCount);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::FlushQuadBatch(tVtxBatchFlag aTypeFlags, bool abAutoClear) {
	if (mSimpleShader)
		mSimpleShader->Bind();
	UploadShaderMatrix();
	SetVtxBatchStates(aTypeFlags);
	SetUpBatchArrays();

	// GL_QUADS is not in GLES2/WebGL. Expand each group of 4 indices into 6
	// indices for two triangles: [0,1,2, 2,3,0].
	if (mlIndexCount >= 4 && (mlIndexCount % 4) == 0) {
		const unsigned int quadCount = mlIndexCount / 4;
		Common::Array<unsigned int> triIndices;
		triIndices.reserve(quadCount * 6);
		for (unsigned int i = 0; i < quadCount; ++i) {
			const unsigned int *q = &mpIndexArray[i * 4];
			triIndices.push_back(q[0]);
			triIndices.push_back(q[1]);
			triIndices.push_back(q[2]);
			triIndices.push_back(q[2]);
			triIndices.push_back(q[3]);
			triIndices.push_back(q[0]);
		}
		GL_CHECK(glDrawElements(GL_TRIANGLES, (GLsizei)triIndices.size(),
								GL_UNSIGNED_INT, triIndices.data()));
	}
	if (abAutoClear)
		flushAutoClear(mlIndexCount, mlVertexCount, mlTexCoordArrayCount);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::ClearBatch() {
	mlIndexCount = 0;
	mlVertexCount = 0;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::DrawLine(const cVector3f &avBegin, const cVector3f &avEnd, cColor aCol) {
	const float positions[] = {
		avBegin.x, avBegin.y, avBegin.z,
		avEnd.x,   avEnd.y,   avEnd.z,
	};
	SetTexture(0, nullptr);
	DrawPrimitiveLines(GL_LINES, positions, 2, aCol);
}

void cLowLevelGraphicsGLES::DrawBoxMaxMin(const cVector3f &avMax, const cVector3f &avMin, cColor aCol) {
	// 12 edges × 2 endpoints = 24 vertices.
	const float positions[] = {
		// +Z quad
		avMax.x, avMax.y, avMax.z,   avMin.x, avMax.y, avMax.z,
		avMax.x, avMax.y, avMax.z,   avMax.x, avMin.y, avMax.z,
		avMin.x, avMax.y, avMax.z,   avMin.x, avMin.y, avMax.z,
		avMin.x, avMin.y, avMax.z,   avMax.x, avMin.y, avMax.z,
		// -Z quad
		avMax.x, avMax.y, avMin.z,   avMin.x, avMax.y, avMin.z,
		avMax.x, avMax.y, avMin.z,   avMax.x, avMin.y, avMin.z,
		avMin.x, avMax.y, avMin.z,   avMin.x, avMin.y, avMin.z,
		avMin.x, avMin.y, avMin.z,   avMax.x, avMin.y, avMin.z,
		// connectors between
		avMax.x, avMax.y, avMax.z,   avMax.x, avMax.y, avMin.z,
		avMin.x, avMax.y, avMax.z,   avMin.x, avMax.y, avMin.z,
		avMin.x, avMin.y, avMax.z,   avMin.x, avMin.y, avMin.z,
		avMax.x, avMin.y, avMax.z,   avMax.x, avMin.y, avMin.z,
	};
	SetTexture(0, nullptr);
	SetBlendActive(false);
	DrawPrimitiveLines(GL_LINES, positions, 24, aCol);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::DrawSphere(const cVector3f &avPos, float afRadius, cColor aCol) {
	const int kSegments = 32;
	const float kStep = k2Pif / (float)kSegments;
	Common::Array<float> positions;
	positions.reserve(kSegments * 3 * 2 * 3); // 3 circles × N segs × 2 endpoints × 3 floats
	// X circle (YZ plane through avPos.x).
	for (int i = 0; i < kSegments; ++i) {
		const float a = i * kStep, b = (i + 1) * kStep;
		positions.push_back(avPos.x); positions.push_back(avPos.y + sinf(a) * afRadius); positions.push_back(avPos.z + cosf(a) * afRadius);
		positions.push_back(avPos.x); positions.push_back(avPos.y + sinf(b) * afRadius); positions.push_back(avPos.z + cosf(b) * afRadius);
	}
	// Y circle (XZ plane through avPos.y).
	for (int i = 0; i < kSegments; ++i) {
		const float a = i * kStep, b = (i + 1) * kStep;
		positions.push_back(avPos.x + cosf(a) * afRadius); positions.push_back(avPos.y); positions.push_back(avPos.z + sinf(a) * afRadius);
		positions.push_back(avPos.x + cosf(b) * afRadius); positions.push_back(avPos.y); positions.push_back(avPos.z + sinf(b) * afRadius);
	}
	// Z circle (XY plane through avPos.z).
	for (int i = 0; i < kSegments; ++i) {
		const float a = i * kStep, b = (i + 1) * kStep;
		positions.push_back(avPos.x + cosf(a) * afRadius); positions.push_back(avPos.y + sinf(a) * afRadius); positions.push_back(avPos.z);
		positions.push_back(avPos.x + cosf(b) * afRadius); positions.push_back(avPos.y + sinf(b) * afRadius); positions.push_back(avPos.z);
	}
	SetTexture(0, nullptr);
	SetBlendActive(false);
	DrawPrimitiveLines(GL_LINES, positions.data(), (int)(positions.size() / 3), aCol);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::DrawLine2D(const cVector2f &avBegin, const cVector2f &avEnd, float afZ, cColor aCol) {
	const float positions[] = {
		avBegin.x, avBegin.y, afZ,
		avEnd.x,   avEnd.y,   afZ,
	};
	SetTexture(0, nullptr);
	SetBlendActive(false);
	DrawPrimitiveLines(GL_LINES, positions, 2, aCol);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::DrawLineRect2D(const cRect2f &aRect, float afZ, cColor aCol) {
	const float positions[] = {
		aRect.x,          aRect.y,          afZ,
		aRect.x + aRect.w, aRect.y,          afZ,
		aRect.x + aRect.w, aRect.y + aRect.h, afZ,
		aRect.x,          aRect.y + aRect.h, afZ,
		aRect.x,          aRect.y,          afZ,
	};
	SetTexture(0, nullptr);
	SetBlendActive(false);
	DrawPrimitiveLines(GL_LINE_STRIP, positions, 5, aCol);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::DrawFilledRect2D(const cRect2f &aRect, float afZ, cColor aCol) {
	// Two triangles covering the rect (CCW from front so the trans pass
	// disable-cull setting in Renderer3D::RenderTrans applies, and the
	// opaque-pass cull doesn't kill this debug overlay).
	const float positions[] = {
		aRect.x,          aRect.y,          afZ,
		aRect.x + aRect.w, aRect.y,          afZ,
		aRect.x + aRect.w, aRect.y + aRect.h, afZ,
		aRect.x,          aRect.y,          afZ,
		aRect.x + aRect.w, aRect.y + aRect.h, afZ,
		aRect.x,          aRect.y + aRect.h, afZ,
	};
	SetTexture(0, nullptr);
	DrawPrimitiveLines(GL_TRIANGLES, positions, 6, aCol);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::DrawLineCircle2D(const cVector2f &avCenter, float afRadius, float afZ, cColor aCol) {
	const int kSegments = 32;
	const float kStep = k2Pif / (float)kSegments;
	float positions[(kSegments + 1) * 3];
	for (int i = 0; i <= kSegments; ++i) {
		const float a = i * kStep;
		positions[i * 3 + 0] = avCenter.x + cosf(a) * afRadius;
		positions[i * 3 + 1] = avCenter.y + sinf(a) * afRadius;
		positions[i * 3 + 2] = afZ;
	}
	SetTexture(0, nullptr);
	SetBlendActive(false);
	DrawPrimitiveLines(GL_LINE_STRIP, positions, kSegments + 1, aCol);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::CopyContextToTexure(iTexture *apTex, const cVector2l &avPos,
											   const cVector2l &avSize, const cVector2l &avTexOffset) {
	if (apTex == nullptr)
		return;

	int lScreenY = (mvScreenSize.y - avSize.y) - avPos.y;
	int lTexY = (apTex->getHeight() - avSize.y) - avTexOffset.y;

	// Log("TExoffset: %d %d\n",avTexOffset.x,lTexY);
	// Log("ScreenOffset: %d %d (h: %d s: %d p: %d)\n",avPos.x,lScreenY,mvScreenSize.y,
	//												avSize.y,avPos.y);

	g_system->presentBuffer();
	SetTexture(0, apTex);
	GL_CHECK(glCopyTexSubImage2D(GetGLTextureTargetEnum(apTex->GetTarget()), 0,
								 avTexOffset.x, lTexY, avPos.x, lScreenY, avSize.x, avSize.y));
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetRenderTarget(iTexture *pTex) {
	if (pTex == mpRenderTarget)
		return;
	mpRenderTarget = pTex;

	if (pTex == nullptr) {
		// Restore the default framebuffer (the canvas) and the full
		// screen-size viewport. Don't tear down mFBO — we re-use it on
		// the next SetRenderTarget(non-null).
		GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
		GL_CHECK(glViewport(0, 0, (int)mvScreenSize.x, (int)mvScreenSize.y));
		return;
	}

	cSDLTexture *pSDLTex = static_cast<cSDLTexture *>(pTex);
	const GLuint texHandle = (GLuint)pSDLTex->GetHandle();
	const int texW = pTex->getWidth();
	const int texH = pTex->getHeight();

	if (!mFBO)
		GL_CHECK(glGenFramebuffers(1, &mFBO));
	GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, mFBO));

	// (Re)allocate a depth renderbuffer matching the target's size. Most
	// post-effect render targets are stable in size so this hits the early
	// exit after the first call.
	if (!mFBODepthRB || mFBODepthW != texW || mFBODepthH != texH) {
		if (!mFBODepthRB)
			GL_CHECK(glGenRenderbuffers(1, &mFBODepthRB));
		GL_CHECK(glBindRenderbuffer(GL_RENDERBUFFER, mFBODepthRB));
		GL_CHECK(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, texW, texH));
		mFBODepthW = texW;
		mFBODepthH = texH;
	}

	GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
									GetGLTextureTargetEnum(pTex->GetTarget()),
									texHandle, 0));
	GL_CHECK(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
									   GL_RENDERBUFFER, mFBODepthRB));
	const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		warning("[HPL1-GLES2] FBO incomplete (status=0x%x) for tex '%s' %dx%d",
				(unsigned)status, pTex->GetName().c_str(), texW, texH);
	}
	GL_CHECK(glViewport(0, 0, texW, texH));
}

//-----------------------------------------------------------------------

bool cLowLevelGraphicsGLES::RenderTargetHasZBuffer() {
	return true;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::FlushRenderTarget() {

	// Old OGL 1.1 Code:
	/*if(mpRenderTarget!=NULL)
	{
		SetTexture(0, mpRenderTarget);

		//Log("w: %d\n",mpRenderTarget->GetWidth());

		glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0,
						mpRenderTarget->GetWidth(), mpRenderTarget->GetHeight(), 0);
	}*/
}

//-----------------------------------------------------------------------

cVector2f cLowLevelGraphicsGLES::GetScreenSize() {
	return cVector2f((float)mvScreenSize.x, (float)mvScreenSize.y);
}

//-----------------------------------------------------------------------

cVector2f cLowLevelGraphicsGLES::GetVirtualSize() {
	return mvVirtualSize;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetVirtualSize(cVector2f avSize) {
	mvVirtualSize = avSize;
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetUpBatchArrays() {
	// GLES2: route batch arrays through the generic-attribute slots that the
	// simple shader is bound to.
	glVertexAttribPointer(eVtxAttr_Position, 3, GL_FLOAT, false,
						  sizeof(float) * mlBatchStride, mpVertexArray);
	glVertexAttribPointer(eVtxAttr_Color0, 4, GL_FLOAT, false,
						  sizeof(float) * mlBatchStride, &mpVertexArray[3]);
	glVertexAttribPointer(eVtxAttr_Normal, 3, GL_FLOAT, false,
						  sizeof(float) * mlBatchStride, &mpVertexArray[10]);
	glVertexAttribPointer(eVtxAttr_Texture0, 3, GL_FLOAT, false,
						  sizeof(float) * mlBatchStride, &mpVertexArray[7]);
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetVtxBatchStates(tVtxBatchFlag flags) {
	if (flags & eVtxBatchFlag_Position)
		glEnableVertexAttribArray(eVtxAttr_Position);
	else
		glDisableVertexAttribArray(eVtxAttr_Position);

	if (flags & eVtxBatchFlag_Color0)
		glEnableVertexAttribArray(eVtxAttr_Color0);
	else
		glDisableVertexAttribArray(eVtxAttr_Color0);

	if (flags & eVtxBatchFlag_Normal)
		glEnableVertexAttribArray(eVtxAttr_Normal);
	else
		glDisableVertexAttribArray(eVtxAttr_Normal);

	if (flags & eVtxBatchFlag_Texture0)
		glEnableVertexAttribArray(eVtxAttr_Texture0);
	else
		glDisableVertexAttribArray(eVtxAttr_Texture0);
}

//-----------------------------------------------------------------------

GLenum cLowLevelGraphicsGLES::GetGLBlendEnum(eBlendFunc type) {
	switch (type) {
	case eBlendFunc_Zero:
		return GL_ZERO;
	case eBlendFunc_One:
		return GL_ONE;
	case eBlendFunc_SrcColor:
		return GL_SRC_COLOR;
	case eBlendFunc_OneMinusSrcColor:
		return GL_ONE_MINUS_SRC_COLOR;
	case eBlendFunc_DestColor:
		return GL_DST_COLOR;
	case eBlendFunc_OneMinusDestColor:
		return GL_ONE_MINUS_DST_COLOR;
	case eBlendFunc_SrcAlpha:
		return GL_SRC_ALPHA;
	case eBlendFunc_OneMinusSrcAlpha:
		return GL_ONE_MINUS_SRC_ALPHA;
	case eBlendFunc_DestAlpha:
		return GL_DST_ALPHA;
	case eBlendFunc_OneMinusDestAlpha:
		return GL_ONE_MINUS_DST_ALPHA;
	case eBlendFunc_SrcAlphaSaturate:
		return GL_SRC_ALPHA_SATURATE;
	default:
		break;
	}
	Hpl1::logError(Hpl1::kDebugOpenGL, "invalid blend op (%d)", type);
	return 0;
}

//-----------------------------------------------------------------------

GLenum cLowLevelGraphicsGLES::GetGLTextureParamEnum(eTextureParam type) {
	// Fixed-function texture combiner enums don't exist in GLES2; this helper
	// is only consulted by SetTextureEnv which is a stub on this path.
	(void)type;
	return 0;
}

//-----------------------------------------------------------------------

GLenum cLowLevelGraphicsGLES::GetGLTextureOpEnum(eTextureOp type) {
	switch (type) {
	case eTextureOp_Color:
		return GL_SRC_COLOR;
	case eTextureOp_OneMinusColor:
		return GL_ONE_MINUS_SRC_COLOR;
	case eTextureOp_Alpha:
		return GL_SRC_ALPHA;
	case eTextureOp_OneMinusAlpha:
		return GL_ONE_MINUS_SRC_ALPHA;
	default:
		break;
	}
	Hpl1::logError(Hpl1::kDebugOpenGL, "invalid texture op (%d)", type);
	return 0;
}

//-----------------------------------------------------------------------

GLenum cLowLevelGraphicsGLES::GetGLTextureSourceEnum(eTextureSource type) {
	switch (type) {
	case eTextureSource_Texture:
		return GL_TEXTURE;
	default:
		break;
	}
	Hpl1::logError(Hpl1::kDebugOpenGL, "invalid texture source (%d)", type);
	return 0;
}
//-----------------------------------------------------------------------

GLenum cLowLevelGraphicsGLES::GetGLTextureTargetEnum(eTextureTarget type) {
	switch (type) {
	case eTextureTarget_1D:
		// No GL_TEXTURE_1D in GLES2; cSDLTexture maps these to 2D textures
		// of height 1 (and cCGProgram polyfills texture1D() accordingly).
		return GL_TEXTURE_2D;
	case eTextureTarget_2D:
		return GL_TEXTURE_2D;
	case eTextureTarget_Rect:
		// No GL_TEXTURE_RECTANGLE in GLES2; cSDLTexture maps these to 2D
		// and cCGProgram polyfills texture2DRect() to use normalized UVs.
		return GL_TEXTURE_2D;
	case eTextureTarget_CubeMap:
		return GL_TEXTURE_CUBE_MAP;
	case eTextureTarget_3D:
		return GL_TEXTURE_3D;
	default:
		break;
	}
	Hpl1::logError(Hpl1::kDebugOpenGL, "invalid texture target (%d)", type);
	return 0;
}

//-----------------------------------------------------------------------

GLenum cLowLevelGraphicsGLES::GetGLTextureFuncEnum(eTextureFunc type) {
	// Fixed-function combiner functions don't exist in GLES2.
	(void)type;
	return 0;
}

//-----------------------------------------------------------------------
GLenum cLowLevelGraphicsGLES::GetGLDepthTestFuncEnum(eDepthTestFunc type) {
	switch (type) {
	case eDepthTestFunc_Never:
		return GL_NEVER;
	case eDepthTestFunc_Less:
		return GL_LESS;
	case eDepthTestFunc_LessOrEqual:
		return GL_LEQUAL;
	case eDepthTestFunc_Greater:
		return GL_GREATER;
	case eDepthTestFunc_GreaterOrEqual:
		return GL_GEQUAL;
	case eDepthTestFunc_Equal:
		return GL_EQUAL;
	case eDepthTestFunc_NotEqual:
		return GL_NOTEQUAL;
	case eDepthTestFunc_Always:
		return GL_ALWAYS;
	default:
		break;
	}
	Hpl1::logError(Hpl1::kDebugOpenGL, "invalid depth test function (%d)", type);
	return 0;
}

//-----------------------------------------------------------------------

GLenum cLowLevelGraphicsGLES::GetGLAlphaTestFuncEnum(eAlphaTestFunc type) {
	switch (type) {
	case eAlphaTestFunc_Never:
		return GL_NEVER;
	case eAlphaTestFunc_Less:
		return GL_LESS;
	case eAlphaTestFunc_LessOrEqual:
		return GL_LEQUAL;
	case eAlphaTestFunc_Greater:
		return GL_GREATER;
	case eAlphaTestFunc_GreaterOrEqual:
		return GL_GEQUAL;
	case eAlphaTestFunc_Equal:
		return GL_EQUAL;
	case eAlphaTestFunc_NotEqual:
		return GL_NOTEQUAL;
	case eAlphaTestFunc_Always:
		return GL_ALWAYS;
	default:
		break;
	}
	Hpl1::logError(Hpl1::kDebugOpenGL, "invalid alpha test function (%d)", type);
	return 0;
}

//-----------------------------------------------------------------------

GLenum cLowLevelGraphicsGLES::GetGLStencilFuncEnum(eStencilFunc type) {
	switch (type) {
	case eStencilFunc_Never:
		return GL_NEVER;
	case eStencilFunc_Less:
		return GL_LESS;
	case eStencilFunc_LessOrEqual:
		return GL_LEQUAL;
	case eStencilFunc_Greater:
		return GL_GREATER;
	case eStencilFunc_GreaterOrEqual:
		return GL_GEQUAL;
	case eStencilFunc_Equal:
		return GL_EQUAL;
	case eStencilFunc_NotEqual:
		return GL_NOTEQUAL;
	case eStencilFunc_Always:
		return GL_ALWAYS;
	default:
		break;
	}
	Hpl1::logError(Hpl1::kDebugOpenGL, "invalid stencil function (%d)", type);
	return 0;
}

//-----------------------------------------------------------------------

GLenum cLowLevelGraphicsGLES::GetGLStencilOpEnum(eStencilOp type) {
	switch (type) {
	case eStencilOp_Keep:
		return GL_KEEP;
	case eStencilOp_Zero:
		return GL_ZERO;
	case eStencilOp_Replace:
		return GL_REPLACE;
	case eStencilOp_Increment:
		return GL_INCR;
	case eStencilOp_Decrement:
		return GL_DECR;
	case eStencilOp_Invert:
		return GL_INVERT;
	case eStencilOp_IncrementWrap:
		return GL_INCR_WRAP;
	case eStencilOp_DecrementWrap:
		return GL_DECR_WRAP;
	default:
		break;
	}
	Hpl1::logError(Hpl1::kDebugOpenGL, "invalid stencil op (%d)", type);
	return 0;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::SetMatrixMode(eMatrix type) {
	// No fixed-function matrix stack in GLES2; the matrix-aware entry points
	// (PushMatrix/SetMatrix/...) maintain mMatrixStack themselves.
	(void)type;
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::NotifyShaderBound(iGpuProgram *prog) {
	mpActiveShader = prog;
	// Re-push the cached pipeline-state uniforms to whatever just got bound,
	// since the engine may set this state before a Bind() call. Cheap; missing
	// uniforms are silently dropped by the GL driver.
	if (mpActiveShader) {
		mpActiveShader->SetFloat("_hpl1_alphaRef", mAlphaTestRef);
		mpActiveShader->SetFloat("_hpl1_alphaOverride", mAlphaOverride);
		mpActiveShader->SetVec4f("_hpl1_constantColor",
								 mConstantColor[0], mConstantColor[1],
								 mConstantColor[2], mConstantColor[3]);
	}
}

//-----------------------------------------------------------------------

void cLowLevelGraphicsGLES::UploadShaderMatrix() {
	// Push the current proj*modelview to whichever program is bound — falling
	// back to mSimpleShader only when no caller has set its own. Targeting
	// mSimpleShader unconditionally (the old behavior) silently failed for
	// post-effect passes that bound their own program: the blur/bloom
	// worldViewProj never got updated, and the blur quad rendered with
	// stale identity matrix.
	iGpuProgram *target = mpActiveShader ? mpActiveShader : mSimpleShader;
	if (!target)
		return;
	cMatrixf mtx = cMath::MatrixMul(mMatrixStack[eMatrix_Projection].top(),
									mMatrixStack[eMatrix_ModelView].top());
	target->SetMatrixf("worldViewProj", mtx);
}

//-----------------------------------------------------------------------

} // namespace hpl

#endif // USE_FORCED_GLES2
#endif // HPL1_USE_OPENGL
