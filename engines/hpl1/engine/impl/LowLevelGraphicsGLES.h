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

#ifndef HPL_LOWLEVELGRAPHICS_GLES_H
#define HPL_LOWLEVELGRAPHICS_GLES_H

#include "common/ptr.h"
#include "common/stack.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"
#include "hpl1/engine/graphics/LowLevelGraphics.h"
#include "hpl1/engine/math/MathTypes.h"
#include "hpl1/opengl.h"


#ifdef HPL1_USE_OPENGL
#if USE_FORCED_GLES2

// Generic vertex attribute locations used by the GLES2 path. Bound by
// cCGProgram via glBindAttribLocation and consumed by cVertexBufferVBO /
// cLowLevelGraphicsGLES's batch arrays. Desktop GL doesn't need these — the
// fixed-function client-state arrays handle vertex data routing.
#define eVtxAttr_Position 0
#define eVtxAttr_Normal   1
#define eVtxAttr_Color0   2
#define eVtxAttr_Texture0 3
#define eVtxAttr_Tangent  4

namespace hpl {

//-------------------------------------------------

GLenum TextureTargetToGL(eTextureTarget target);

//-------------------------------------------------

class cLowLevelGraphicsGLES : public iLowLevelGraphics {
public:
	cLowLevelGraphicsGLES();
	~cLowLevelGraphicsGLES();

	bool Init(int alWidth, int alHeight, int alBpp, int abFullscreen, int alMultisampling,
			  const tString &asWindowCaption);

	int GetCaps(eGraphicCaps aType) const;

	void ShowCursor(bool abX);

	void SetMultisamplingActive(bool abX);

	void SetGammaCorrection(float afX);
	float GetGammaCorrection();

	int GetMultisampling() { return mlMultisampling; }

	void SetClipPlane(int alIdx, const cPlanef &aPlane);
	cPlanef GetClipPlane(int alIdx, const cPlanef &aPlane);
	void SetClipPlaneActive(int alIdx, bool abX);

	cVector2f GetScreenSize();
	cVector2f GetVirtualSize();
	void SetVirtualSize(cVector2f avSize);

	Bitmap2D *CreateBitmap2D(const cVector2l &avSize);
	FontData *CreateFontData(const tString &asName);

	iTexture *CreateTexture(bool abUseMipMaps, eTextureType aType, eTextureTarget aTarget);
	iTexture *CreateTexture(const tString &asName, bool abUseMipMaps, eTextureType aType, eTextureTarget aTarget);
	iTexture *CreateTexture(Bitmap2D *apBmp, bool abUseMipMaps, eTextureType aType, eTextureTarget aTarget);
	iTexture *CreateTexture(const cVector2l &avSize, int alBpp, cColor aFillCol,
							bool abUseMipMaps, eTextureType aType, eTextureTarget aTarget);

	Graphics::PixelFormat *GetPixelFormat();

	iGpuProgram *CreateGpuProgram(const tString &vertex, const tString &fragment);
	iGpuProgram *GetSimpleShader() override { return mSimpleShader; }

	void SaveScreenToBMP(const tString &asFile);

	/////////// MATRIX METHODS /////////////////////////

	void PushMatrix(eMatrix aMtxType);
	void PopMatrix(eMatrix aMtxType);
	void SetIdentityMatrix(eMatrix aMtxType);

	void SetMatrix(eMatrix aMtxType, const cMatrixf &a_mtxA);

	void TranslateMatrix(eMatrix aMtxType, const cVector3f &avPos);
	void RotateMatrix(eMatrix aMtxType, const cVector3f &avRot);
	void ScaleMatrix(eMatrix aMtxType, const cVector3f &avScale);

	void SetOrthoProjection(const cVector2f &avSize, float afMin, float afMax);

	/////////// DRAWING METHODS /////////////////////////

	// OCCLUSION
	iOcclusionQuery *CreateOcclusionQuery();
	void DestroyOcclusionQuery(iOcclusionQuery *apQuery);

	// CLEARING THE FRAMEBUFFER
	void ClearScreen();

	void SetClearColor(const cColor &aCol);
	void SetClearDepth(float afDepth);
	void SetClearStencil(int alVal);

	void SetClearColorActive(bool abX);
	void SetClearDepthActive(bool abX);
	void SetClearStencilActive(bool abX);

	void SetColorWriteActive(bool abR, bool abG, bool abB, bool abA);
	void SetDepthWriteActive(bool abX);

	void SetCullActive(bool abX);
	void SetCullMode(eCullMode aMode);

	// DEPTH
	void SetDepthTestActive(bool abX);
	void SetDepthTestFunc(eDepthTestFunc aFunc);

	// ALPHA
	void SetAlphaTestActive(bool abX);
	void SetAlphaTestFunc(eAlphaTestFunc aFunc, float afRef);

	// STENCIL
	void SetStencilActive(bool abX);
	/*void SetStencilTwoSideActive(bool abX);
	void SetStencilFace(eStencilFace aFace);
	void SetStencilFunc(eStencilFunc aFunc,int alRef, unsigned int aMask);
	void SetStencilOp(eStencilOp aFailOp,eStencilOp aZFailOp,eStencilOp aZPassOp);*/
	void SetStencil(eStencilFunc aFunc, int alRef, unsigned int aMask,
					eStencilOp aFailOp, eStencilOp aZFailOp, eStencilOp aZPassOp);
	void SetStencilTwoSide(eStencilFunc aFrontFunc, eStencilFunc aBackFunc,
						   int alRef, unsigned int aMask,
						   eStencilOp aFrontFailOp, eStencilOp aFrontZFailOp, eStencilOp aFrontZPassOp,
						   eStencilOp aBackFailOp, eStencilOp aBackZFailOp, eStencilOp aBackZPassOp);
	void SetStencilTwoSide(bool abX);

	// SCISSOR
	void SetScissorActive(bool abX);
	void SetScissorRect(const cRect2l &aRect);

	// BLENDING
	void SetBlendActive(bool abX);
	void SetBlendFunc(eBlendFunc aSrcFactor, eBlendFunc aDestFactor);
	void SetBlendFuncSeparate(eBlendFunc aSrcFactorColor, eBlendFunc aDestFactorColor,
							  eBlendFunc aSrcFactorAlpha, eBlendFunc aDestFactorAlpha);

	// TEXTURE
	void SetTexture(unsigned int alUnit, iTexture *apTex);
	void SetActiveTextureUnit(unsigned int alUnit);
	void SetTextureEnv(eTextureParam aParam, int alVal);
	void SetTextureConstantColor(const cColor &color);

	void SetColor(const cColor &aColor);

	// POLYGONS
	iVertexBuffer *CreateVertexBuffer(tVertexFlag aFlags, eVertexBufferDrawType aDrawType,
									  eVertexBufferUsageType aUsageType,
									  int alReserveVtxSize = 0, int alReserveIdxSize = 0);

	void DrawRect(const cVector2f &avPos, const cVector2f &avSize, float afZ);

	void DrawTri(const tVertexVec &avVtx);
	void DrawTri(const cVertex *avVtx);

	void DrawQuad(const tVertexVec &avVtx);
	void DrawQuad(const tVertexVec &avVtx, const cColor aCol);
	void DrawQuad(const tVertexVec &avVtx, const float afZ);
	void DrawQuad(const tVertexVec &avVtx, const float afZ, const cColor &aCol);
	void DrawQuadMultiTex(const tVertexVec &avVtx, const tVector3fVec &avExtraUvs);

	void AddVertexToBatch(const cVertex &apVtx);
	void AddVertexToBatch(const cVertex *apVtx, const cVector3f *avTransform);
	void AddVertexToBatch(const cVertex *apVtx, const cMatrixf *aMtx);

	void AddVertexToBatch_Size2D(const cVertex *apVtx, const cVector3f *avTransform,
								 const cColor *apCol, const float &mfW, const float &mfH);

	void AddVertexToBatch_Raw(const cVector3f &avPos, const cColor &aColor,
							  const cVector3f &avTex);

	void AddTexCoordToBatch(unsigned int alUnit, const cVector3f *apCoord);
	void SetBatchTextureUnitActive(unsigned int alUnit, bool abActive);

	void AddIndexToBatch(int alIndex);

	void FlushTriBatch(tVtxBatchFlag aTypeFlags, bool abAutoClear = true);
	void FlushQuadBatch(tVtxBatchFlag aTypeFlags, bool abAutoClear = true);
	void ClearBatch();

	// PRIMITIVES
	void DrawLine(const cVector3f &avBegin, const cVector3f &avEnd, cColor aCol);
	void DrawBoxMaxMin(const cVector3f &avMax, const cVector3f &avMin, cColor aCol);
	void DrawSphere(const cVector3f &avPos, float afRadius, cColor aCol);

	void DrawLine2D(const cVector2f &avBegin, const cVector2f &avEnd, float afZ, cColor aCol);
	void DrawLineRect2D(const cRect2f &aRect, float afZ, cColor aCol);
	void DrawLineCircle2D(const cVector2f &avCenter, float afRadius, float afZ, cColor aCol);

	void DrawFilledRect2D(const cRect2f &aRect, float afZ, cColor aCol);

	// FRAMEBUFFER
	void CopyContextToTexure(iTexture *apTex, const cVector2l &avPos,
							 const cVector2l &avSize, const cVector2l &avTexOffset = 0);
	void SetRenderTarget(iTexture *pTex);
	bool RenderTargetHasZBuffer();
	void FlushRenderTarget();

	void FlushRendering();
	void SwapBuffers();

	///// GL Specific /////////////////////////////

	void SetupGL();

	GLenum GetGLTextureTargetEnum(eTextureTarget aType);

private:
	cVector2l mvScreenSize;
	cVector2f mvVirtualSize;
	int mlMultisampling;
	int mlBpp;

	// Gamma
	// uint16 mvStartGammaArray[3][256];
	float mfGammaCorrection;

	// Clipping
	cPlanef mvClipPlanes[kMaxClipPlanes];

	// SDL Variables
	// SDL_Surface *mpScreen;
	Graphics::PixelFormat mpPixelFormat;

	// Vertex Array variables
	// The vertex arrays used:
	float *mpVertexArray;
	unsigned int mlVertexCount;
	unsigned int *mpIndexArray;
	unsigned int mlIndexCount;

	unsigned int mlBatchStride;

	float *mpTexCoordArray[MAX_TEXTUREUNITS];
	bool mbTexCoordArrayActive[MAX_TEXTUREUNITS];
	unsigned int mlTexCoordArrayCount[MAX_TEXTUREUNITS];

	unsigned int mlBatchArraySize;

	// Clearing
	bool mbClearColor;
	bool mbClearDepth;
	bool mbClearStencil;

	// Rendertarget variables
	iTexture *mpRenderTarget;

	// Texture
	iTexture *mpCurrentTexture[MAX_TEXTUREUNITS];

	iTexture *_screenBuffer;
	iGpuProgram *_gammaCorrectionProgram;
	// FBO state for SetRenderTarget. Created lazily on first non-null
	// target. The depth renderbuffer is reallocated when the bound target
	// changes size — usually post-effect refraction textures are small
	// and stable, so this happens at most a handful of times per session.
	unsigned int mFBO = 0;
	unsigned int mFBODepthRB = 0;
	int mFBODepthW = 0;
	int mFBODepthH = 0;

	// CG Compiler Variables
	// CGcontext mCG_Context;

	// Multisample
	void CheckMultisampleCaps();

	void applyGammaCorrection();

	// Batch helper
	void SetUpBatchArrays();

	// Depth helper
	GLenum GetGLDepthTestFuncEnum(eDepthTestFunc aType);

	// Alpha Helper
	GLenum GetGLAlphaTestFuncEnum(eAlphaTestFunc aType);

	// Stencil helper
	GLenum GetGLStencilFuncEnum(eStencilFunc aType);
	GLenum GetGLStencilOpEnum(eStencilOp aType);

	// Matrix Helper
	void SetMatrixMode(eMatrix mType);

	// Texture helper
	GLenum GetGLTextureParamEnum(eTextureParam aType);
	GLenum GetGLTextureOpEnum(eTextureOp aType);
	GLenum GetGLTextureFuncEnum(eTextureFunc aType);
	GLenum GetGLTextureSourceEnum(eTextureSource aType);

	// Blend helper
	GLenum GetGLBlendEnum(eBlendFunc aType);

	// Vtx helper
	void SetVtxBatchStates(tVtxBatchFlag flags);

public:
	// Read the top of a matrix stack. Used by cCGProgram::SetMatrixf when a
	// material shader needs the current modelview/projection (the GLES2
	// equivalent of glGetFloatv(GL_MODELVIEW_MATRIX) on desktop GL).
	const cMatrixf &GetMatrixStackTop(eMatrix aMtxType) const { return mMatrixStack[aMtxType].top(); }

private:
	// OpenGL ES software matrix stack
	Common::Stack<cMatrixf> mMatrixStack[eMatrix_LastEnum];
	void UploadShaderMatrix();
	// Common setup for the legacy immediate-mode debug-draw helpers
	// (DrawLine, DrawBoxMaxMin, DrawSphere, DrawLineRect2D, ...). Binds
	// mSimpleShader when nothing else is active, pushes the current
	// proj*modelview, and writes a constant vertex color via
	// glVertexAttrib4f so the caller doesn't need to upload a per-vertex
	// color buffer. Then issues glDrawArrays(`aMode`, ...).
	void DrawPrimitiveLines(unsigned int aMode, const float *positions,
							int aVertexCount, const cColor &aColor);

	// Simple shader used to drive the GLES2 fixed-function emulation path
	// (DrawQuad / FlushTriBatch / FlushQuadBatch).
	iGpuProgram *mSimpleShader = nullptr;
	// Most-recently bound program, tracked via cCGProgram::Bind/UnBind so
	// DrawQuad's fallback (binds mSimpleShader if nothing else is in use)
	// doesn't clobber a caller's shader, and UploadShaderMatrix writes to
	// the program that's actually live rather than always to mSimpleShader.
public:
	void NotifyShaderBound(iGpuProgram *prog);
	void NotifyShaderUnbound(iGpuProgram *prog) { if (mpActiveShader == prog) mpActiveShader = nullptr; }
	iGpuProgram *GetActiveShader() const { return mpActiveShader; }
	// Shader-side alpha-test threshold — pushed to whichever program is
	// bound. 0 means "don't discard". See injectAlphaTest in CGProgram.cpp.
	float GetAlphaTestRef() const { return mAlphaTestRef; }
private:
	iGpuProgram *mpActiveShader = nullptr;
	float mAlphaTestRef = 0.0f;
	// Combiner emulation: SetTextureConstantColor stores into mConstantColor,
	// and the SetTextureEnv(AlphaSource0=Constant, AlphaFunc=Replace) pair
	// flips mAlphaOverride to 1 so mSimpleShader's fragment substitutes the
	// constant's alpha for the texture's. Default is 0 (no override) so plain
	// DrawQuad / batch flushes keep the texture's native alpha.
	float mConstantColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	float mAlphaOverride = 0.0f;
	bool mAlphaUsesConstant = false;
	// 1x1 white default texture bound when SetTexture is called with NULL,
	// since GLES2 lacks fixed-function texture disable.
	iTexture *mDefaultTexture = nullptr;
};

} // namespace hpl

#endif // USE_FORCED_GLES2
#endif // HPL1_USE_OPENGL
#endif // HPL_LOWLEVELGRAPHICS_GLES_H
