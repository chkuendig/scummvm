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

#include "hpl1/engine/graphics/RendererPostEffects.h"
#include "hpl1/debug.h"
#include "hpl1/engine/graphics/GPUProgram.h"
#include "hpl1/engine/graphics/LowLevelGraphics.h"
#include "hpl1/engine/graphics/RenderList.h"
#include "hpl1/engine/graphics/Renderer3D.h"
#include "hpl1/engine/graphics/Texture.h"
#include "hpl1/engine/math/Math.h"
#include "hpl1/engine/resources/GpuProgramManager.h"
#include "hpl1/engine/resources/Resources.h"
#include "hpl1/engine/resources/low_level_resources.h"
#include "hpl1/engine/scene/Scene.h"
#include "hpl1/engine/system/low_level_system.h"

namespace hpl {

//////////////////////////////////////////////////////////////////////////
// CONSTRUCTORS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

cRendererPostEffects::cRendererPostEffects(iLowLevelGraphics *apLowLevelGraphics, cResources *apResources,
										   cRenderList *apRenderList, cRenderer3D *apRenderer3D) {
	mpLowLevelGraphics = apLowLevelGraphics;
	mpLowLevelResources = apResources->GetLowLevel();
	mpResources = apResources;
	mpRenderer3D = apRenderer3D;

	mpGpuManager = mpResources->GetGpuProgramManager();

	mvScreenSize = mpLowLevelGraphics->GetScreenSize();

	mpRenderList = apRenderList;

	///////////////////////////////////////////
	Log(" Creating screen buffers size %s\n", mvScreenSize.ToString().c_str());
	for (int i = 0; i < 2; i++) {
		if (mpLowLevelGraphics->GetCaps(eGraphicCaps_TextureTargetRectangle)) {
			mpScreenBuffer[i] = mpLowLevelGraphics->CreateTexture(cVector2l(
																	  (int)mvScreenSize.x, (int)mvScreenSize.y),
																  32, cColor(0, 0, 0, 0), false,
																  eTextureType_Normal, eTextureTarget_Rect);

			if (mpScreenBuffer[i] == NULL) {
				Error("Couldn't create screenbuffer!\n");
				mpScreenBuffer[0] = NULL;
				mpScreenBuffer[1] = NULL;
				break;
			}
			mpScreenBuffer[i]->SetWrapS(eTextureWrap_ClampToEdge);
			mpScreenBuffer[i]->SetWrapT(eTextureWrap_ClampToEdge);

		} else {
			mpScreenBuffer[i] = NULL;
			Error("Texture rectangle not supported. Posteffects will be turned off.");
		}
	}

	///////////////////////////////////////////
	Hpl1::logInfo(Hpl1::kDebugRenderer, "%s", "Creating RendererPostEffects programs");

	/////////////////
	mbBlurFallback = false; // Set to true if the fallbacks are used.
	_blur2DProgram = mpGpuManager->CreateProgram("hpl1_PostEffect_Blur", "hpl1_PostEffect_Blur_2D");
	_blurRectProgram = mpGpuManager->CreateProgram("hpl1_PostEffect_Blur", "hpl1_PostEffect_Blur_Rect");

	/////////////////
	_bloomProgram = mpGpuManager->CreateProgram("hpl1_PostEffect_Bloom", "hpl1_PostEffect_Bloom");

	mpBloomBlurTexture = mpLowLevelGraphics->CreateTexture(
		cVector2l(256, 256),
		32, cColor(0, 0, 0, 0), false,
		eTextureType_Normal, eTextureTarget_2D);

	if (mpBloomBlurTexture == NULL) {
		Error("Couldn't create bloom blur textures!\n");
	} else {
		mpBloomBlurTexture->SetWrapS(eTextureWrap_ClampToEdge);
		mpBloomBlurTexture->SetWrapT(eTextureWrap_ClampToEdge);
	}

	/////////////////
	_motionBlurProgram = mpGpuManager->CreateProgram("hpl1_PostEffect_Motion", "hpl1_PostEffect_Motion"); // CHECK APPLE

	/////////////////
	_depthOfFieldProgram = mpGpuManager->CreateProgram("hpl1_PostEffect_DoF", "hpl1_PostEffect_DoF");

	mpDofBlurTexture = mpLowLevelGraphics->CreateTexture(cVector2l(256, 256),
														 32, cColor(0, 0, 0, 0), false,
														 eTextureType_Normal, eTextureTarget_2D);

	if (mpDofBlurTexture == NULL) {
		Error("Couldn't create Depth of Field blur textures!\n");
	} else {
		mpDofBlurTexture->SetWrapS(eTextureWrap_ClampToEdge);
		mpDofBlurTexture->SetWrapT(eTextureWrap_ClampToEdge);
	}

	Log("RendererPostEffects created\n");

	////////////////////////////////////
	// Variable setup

	// General
	mbActive = false;
	mvTexRectVtx.resize(4);

	// Bloom
	mbBloomActive = false;
	mfBloomSpread = 2.0f;

	// Motion blur
	mbMotionBlurActive = false;
	mfMotionBlurAmount = 1.0f;
	mbMotionBlurFirstTime = true;

	// Depth of Field
	mbDofActive = false;
	mfDofMaxBlur = 1.0f;
	mfDofFocalPlane = 1.0f;
	mfDofNearPlane = 2.0f;
	mfDofFarPlane = 3.0f;
}

//-----------------------------------------------------------------------

cRendererPostEffects::~cRendererPostEffects() {
	for (int i = 0; i < 2; i++)
		if (mpScreenBuffer[i])
			hplDelete(mpScreenBuffer[i]);

	if (_blur2DProgram)
		mpGpuManager->Destroy(_blur2DProgram);
	if (_blurRectProgram)
		mpGpuManager->Destroy(_blurRectProgram);

	if (_bloomProgram)
		mpGpuManager->Destroy(_bloomProgram);

	if (_motionBlurProgram)
		mpGpuManager->Destroy(_motionBlurProgram);

	if (_depthOfFieldProgram)
		mpGpuManager->Destroy(_depthOfFieldProgram);

	if (mpBloomBlurTexture)
		hplDelete(mpBloomBlurTexture);
	if (mpDofBlurTexture)
		hplDelete(mpDofBlurTexture);
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

void cRendererPostEffects::SetBloomActive(bool abX) {
	mbBloomActive = abX;
}

//-----------------------------------------------------------------------

void cRendererPostEffects::SetMotionBlurActive(bool abX) {
	mbMotionBlurActive = abX;
	mbMotionBlurFirstTime = true;
}

//-----------------------------------------------------------------------

void cRendererPostEffects::Render() {
	if (mpScreenBuffer[0] == NULL || mpScreenBuffer[1] == NULL)
		return;
	if (!mpLowLevelGraphics->GetCaps(eGraphicCaps_TextureTargetRectangle))
		return;

	if (mbActive == false ||
		(mImageTrailData.mbActive == false && mbBloomActive == false &&
		 mbMotionBlurActive == false && mbDofActive == false)) {
		return;
	}

	mvScreenSize = mpLowLevelGraphics->GetScreenSize();

	RenderDepthOfField();
	RenderMotionBlur();

	mpLowLevelGraphics->SetDepthTestActive(false);
	mpLowLevelGraphics->PushMatrix(eMatrix_ModelView);
	mpLowLevelGraphics->SetIdentityMatrix(eMatrix_ModelView);
	mpLowLevelGraphics->SetOrthoProjection(mpLowLevelGraphics->GetVirtualSize(), -1000, 1000);

	RenderBloom();

	RenderImageTrail();

	mpLowLevelGraphics->PopMatrix(eMatrix_ModelView);
}

//-----------------------------------------------------------------------

// TODO: Support source texture as 2D

void cRendererPostEffects::RenderBlurTexture(iTexture *apDestination, iTexture *apSource,
											 float afBlurAmount) {
	bool bProgramsLoaded = false;
	if (_blur2DProgram && _blurRectProgram)
		bProgramsLoaded = true;

	iLowLevelGraphics *pLowLevel = mpLowLevelGraphics;
	cVector2l vBlurSize = cVector2l(apDestination->getWidth(), apDestination->getHeight());
	cVector2f vBlurDrawSize;
	vBlurDrawSize.x = ((float)vBlurSize.x / mvScreenSize.x) * mpLowLevelGraphics->GetVirtualSize().x;
	vBlurDrawSize.y = ((float)vBlurSize.y / mvScreenSize.y) * mpLowLevelGraphics->GetVirtualSize().y;

	pLowLevel->SetBlendActive(false);

	///////////////////////////////////////////
	// Horizontal blur pass

	if (bProgramsLoaded) {
		_blurRectProgram->Bind();
		_blurRectProgram->SetFloat("xOffset", 1);
		_blurRectProgram->SetFloat("yOffset", 0);
		_blurRectProgram->SetFloat("amount", afBlurAmount);
		_blurRectProgram->SetMatrixf("worldViewProj", eGpuProgramMatrix_ViewProjection, eGpuProgramMatrixOp_Identity);
	}

	pLowLevel->SetTexture(0, apSource);
	if (mbBlurFallback) {
		mpLowLevelGraphics->SetTexture(1, apSource);
		mpLowLevelGraphics->SetTexture(2, apSource);
	}

	{
		mvTexRectVtx[0] = cVertex(cVector3f(0, 0, 40), cVector2f(0, mvScreenSize.y), cColor(1, 1.0f));
		mvTexRectVtx[1] = cVertex(cVector3f(vBlurDrawSize.x, 0, 40), cVector2f(mvScreenSize.x, mvScreenSize.y), cColor(1, 1.0f));
		mvTexRectVtx[2] = cVertex(cVector3f(vBlurDrawSize.x, vBlurDrawSize.y, 40), cVector2f(mvScreenSize.x, 0), cColor(1, 1.0f));
		mvTexRectVtx[3] = cVertex(cVector3f(0, vBlurDrawSize.y, 40), cVector2f(0, 0), cColor(1, 1.0f));

		mpLowLevelGraphics->DrawQuad(mvTexRectVtx);
	}

	pLowLevel->CopyContextToTexure(apDestination, 0, vBlurSize);

	///////////////////////////////////////////
	// Vertical blur pass

	if (bProgramsLoaded) {
		_blur2DProgram->Bind();
		_blur2DProgram->SetFloat("xOffset", 0);
		_blur2DProgram->SetFloat("yOffset", 1);
		_blur2DProgram->SetFloat("amount", (1 / pLowLevel->GetScreenSize().x) * afBlurAmount);
	}

	pLowLevel->SetTexture(0, apDestination);
	if (mbBlurFallback) {
		mpLowLevelGraphics->SetTexture(1, apDestination);
		mpLowLevelGraphics->SetTexture(2, apDestination);
	}

	{
		mvTexRectVtx[0] = cVertex(cVector3f(0, 0, 40), cVector2f(0, 1), cColor(1, 1.0f));
		mvTexRectVtx[1] = cVertex(cVector3f(vBlurDrawSize.x, 0, 40), cVector2f(1, 1), cColor(1, 1.0f));
		mvTexRectVtx[2] = cVertex(cVector3f(vBlurDrawSize.x, vBlurDrawSize.y, 40), cVector2f(1, 0), cColor(1, 1.0f));
		mvTexRectVtx[3] = cVertex(cVector3f(0, vBlurDrawSize.y, 40), cVector2f(0, 0), cColor(1, 1.0f));

		mpLowLevelGraphics->DrawQuad(mvTexRectVtx);
	}

	if (bProgramsLoaded) {
		_blur2DProgram->UnBind();
	}

	pLowLevel->CopyContextToTexure(apDestination, 0, vBlurSize);

	if (mbBlurFallback) {
		mpLowLevelGraphics->SetTexture(1, NULL);
		mpLowLevelGraphics->SetTexture(2, NULL);
	}
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

void cRendererPostEffects::RenderDepthOfField() {
	if (mbDofActive == false)
		return;

	if (!_depthOfFieldProgram)
		return;

	//////////////////////////////
	cCamera3D *pCam = mpRenderList->GetCamera();

	iTexture *pScreenTexture = mpScreenBuffer[mImageTrailData.mlCurrentBuffer == 0 ? 1 : 0];

	cVector2f vVirtSize = mpLowLevelGraphics->GetVirtualSize();

	mpLowLevelGraphics->CopyContextToTexure(pScreenTexture, 0, cVector2l((int)mvScreenSize.x, (int)mvScreenSize.y));

	mpLowLevelGraphics->SetDepthWriteActive(false);
	mpLowLevelGraphics->SetDepthTestActive(false);
	mpLowLevelGraphics->SetIdentityMatrix(eMatrix_ModelView);
	mpLowLevelGraphics->SetOrthoProjection(mpLowLevelGraphics->GetVirtualSize(), -1000, 1000);

	RenderBlurTexture(mpDofBlurTexture, pScreenTexture, 2.0f);

	mpLowLevelGraphics->SetTexture(0, pScreenTexture);
	mpLowLevelGraphics->SetTexture(1, mpDofBlurTexture);

	// Bind _depthOfFieldProgram once for both passes; the `skyMode` uniform
	// switches behaviour: 1.0 forces a uniform `maxBlur` over the whole quad,
	// 0.0 uses the depth-driven per-pixel computation.
	_depthOfFieldProgram->Bind();
	_depthOfFieldProgram->SetVec3f("planes", cVector3f(mfDofNearPlane, mfDofFocalPlane, mfDofFarPlane));
	_depthOfFieldProgram->SetFloat("maxBlur", mfDofMaxBlur);
	_depthOfFieldProgram->SetVec2f("screenSize", mvScreenSize);

	// Render entire screen textur to screen, this to cover up what has been done by the
	// blur filter, should have support for frame_buffer_object so this can be skipped.
	if (mpRenderer3D->GetSkyBoxActive()) {
		tVector3fVec vUvVec;
		vUvVec.resize(4);

		vUvVec[0] = cVector2f(0, 1);
		vUvVec[1] = cVector2f(1, 1);
		vUvVec[2] = cVector2f(1, 0);
		vUvVec[3] = cVector2f(0, 0);

		mvTexRectVtx[0] = cVertex(cVector3f(0, 0, 40), cVector2f(0, mvScreenSize.y), cColor(1, 1.0f));
		mvTexRectVtx[1] = cVertex(cVector3f(vVirtSize.x, 0, 40), cVector2f(mvScreenSize.x, mvScreenSize.y), cColor(1, 1.0f));
		mvTexRectVtx[2] = cVertex(cVector3f(vVirtSize.x, vVirtSize.y, 40), cVector2f(mvScreenSize.x, 0), cColor(1, 1.0f));
		mvTexRectVtx[3] = cVertex(cVector3f(0, vVirtSize.y, 40), cVector2f(0, 0), cColor(1, 1.0f));

		// Desktop fixed-function path: program a combiner that interpolates
		// the screen rect texture and the blur texture by a constant DoF
		// strength. The shader path achieves the same lerp via skyMode=1.
		mpLowLevelGraphics->SetActiveTextureUnit(1);
		mpLowLevelGraphics->SetTextureEnv(eTextureParam_ColorSource0, eTextureSource_Texture);
		mpLowLevelGraphics->SetTextureEnv(eTextureParam_ColorSource1, eTextureSource_Previous);
		mpLowLevelGraphics->SetTextureEnv(eTextureParam_ColorSource2, eTextureSource_Constant);
		mpLowLevelGraphics->SetTextureEnv(eTextureParam_ColorFunc, eTextureFunc_Interpolate);
		mpLowLevelGraphics->SetTextureConstantColor(cColor(mfDofMaxBlur, mfDofMaxBlur));

		_depthOfFieldProgram->SetFloat("skyMode", 1.0f);
		_depthOfFieldProgram->SetMatrixf("worldViewProj", eGpuProgramMatrix_ViewProjection,
										 eGpuProgramMatrixOp_Identity);

		mpLowLevelGraphics->DrawQuadMultiTex(mvTexRectVtx, vUvVec);

		mpLowLevelGraphics->SetTextureEnv(eTextureParam_ColorFunc, eTextureFunc_Modulate);
	}

	mpLowLevelGraphics->SetMatrix(eMatrix_Projection, pCam->GetProjectionMatrix());

	///////////////////////////////////////////
	// Draw motion blur objects
	mpLowLevelGraphics->SetDepthTestFunc(eDepthTestFunc_Equal);
	mpLowLevelGraphics->SetDepthTestActive(true);
	mpLowLevelGraphics->SetBlendActive(false);

	// Per-mesh DoF: use the depth-driven blur factor.
	_depthOfFieldProgram->SetFloat("skyMode", 0.0f);

	//////////////////
	cMotionBlurObjectIterator it = mpRenderList->GetMotionBlurIterator();
	while (it.HasNext()) {
		iRenderable *pObject = it.Next();
		cMatrixf *pMtx = pObject->GetModelMatrix(pCam);

		//////////////////
		// Non static models
		if (pMtx) {
			mpLowLevelGraphics->SetMatrix(eMatrix_ModelView, cMath::MatrixMul(pCam->GetViewMatrix(), *pMtx));

			_depthOfFieldProgram->SetMatrixf("worldViewProj", eGpuProgramMatrix_ViewProjection,
											 eGpuProgramMatrixOp_Identity);
		}
		//////////////////
		// NULL Model view matrix (static)
		else {
			mpLowLevelGraphics->SetMatrix(eMatrix_ModelView, pCam->GetViewMatrix());

			_depthOfFieldProgram->SetMatrixf("worldViewProj", eGpuProgramMatrix_ViewProjection,
											 eGpuProgramMatrixOp_Identity);
		}

		pObject->GetVertexBuffer()->Bind();

		pObject->GetVertexBuffer()->Draw();

		pObject->GetVertexBuffer()->UnBind();

		if (pMtx)
			pObject->SetPrevMatrix(*pMtx);
	}

	mpLowLevelGraphics->SetDepthTestFunc(eDepthTestFunc_LessOrEqual);
	mpLowLevelGraphics->SetDepthWriteActive(true);

	_depthOfFieldProgram->UnBind();
	mpLowLevelGraphics->SetTexture(0, NULL);
	mpLowLevelGraphics->SetTexture(1, NULL);
}

//-----------------------------------------------------------------------

void cRendererPostEffects::RenderMotionBlur() {
	if (mbMotionBlurActive == false)
		return;

	if (!_motionBlurProgram)
		return;

	//////////////////////////////
	iTexture *pScreenTexture = mpScreenBuffer[mImageTrailData.mlCurrentBuffer == 0 ? 1 : 0];

	mpLowLevelGraphics->CopyContextToTexure(pScreenTexture, 0, cVector2l((int)mvScreenSize.x, (int)mvScreenSize.y));

	///////////////////////////////////////////
	// Draw motion blur objects
	mpLowLevelGraphics->SetDepthTestActive(true);
	mpLowLevelGraphics->SetBlendActive(false);

	cCamera3D *pCam = mpRenderList->GetCamera();
	if (mbMotionBlurFirstTime) {
		pCam->SetPrevView(pCam->GetViewMatrix());
		pCam->SetPrevProjection(pCam->GetProjectionMatrix());
		mbMotionBlurFirstTime = false;
	}

	cMotionBlurObjectIterator it = mpRenderList->GetMotionBlurIterator();

	_motionBlurProgram->Bind();
	_motionBlurProgram->SetFloat("blurScale", mfMotionBlurAmount);
	_motionBlurProgram->SetVec2f("halfScreenSize",
								 cVector2f((float)pScreenTexture->getWidth() / 2.0f,
										   (float)pScreenTexture->getHeight() / 2.0f));

	mpLowLevelGraphics->SetTexture(0, pScreenTexture);

	//////////////////
	while (it.HasNext()) {
		iRenderable *pObject = it.Next();
		cMatrixf *pMtx = pObject->GetModelMatrix(pCam);

		//////////////////
		// Non static models
		if (pMtx) {
			cMatrixf mtxPrev = pObject->GetPrevMatrix();

			mpLowLevelGraphics->SetMatrix(eMatrix_ModelView, cMath::MatrixMul(pCam->GetViewMatrix(), *pMtx));

			_motionBlurProgram->SetMatrixf("worldViewProj", eGpuProgramMatrix_ViewProjection,
										   eGpuProgramMatrixOp_Identity);

			cMatrixf mtxModelView = cMath::MatrixMul(pCam->GetViewMatrix(), *pMtx);
			cMatrixf mtxPrevModelView = cMath::MatrixMul(pCam->GetPrevView(), mtxPrev);

			cMatrixf mtxPrevViewProj = cMath::MatrixMul(pCam->GetPrevProjection(), mtxPrevModelView);

			_motionBlurProgram->SetMatrixf("prevWorldViewProj", mtxPrevViewProj);
			_motionBlurProgram->SetMatrixf("modelView", mtxModelView);
			_motionBlurProgram->SetMatrixf("prevModelView", mtxPrevModelView);
		}
		//////////////////
		// NULL Model view matrix (static)
		else {
			mpLowLevelGraphics->SetMatrix(eMatrix_ModelView, pCam->GetViewMatrix());

			_motionBlurProgram->SetMatrixf("worldViewProj", eGpuProgramMatrix_ViewProjection,
										   eGpuProgramMatrixOp_Identity);

			const cMatrixf &mtxModelView = pCam->GetViewMatrix();
			const cMatrixf &mtxPrevModelView = pCam->GetPrevView();

			cMatrixf mtxPrevViewProj = cMath::MatrixMul(pCam->GetPrevProjection(), mtxPrevModelView);

			_motionBlurProgram->SetMatrixf("prevWorldViewProj", mtxPrevViewProj);
			_motionBlurProgram->SetMatrixf("modelView", mtxModelView);
			_motionBlurProgram->SetMatrixf("prevModelView", mtxPrevModelView);
		}

		pObject->GetVertexBuffer()->Bind();

		pObject->GetVertexBuffer()->Draw();

		pObject->GetVertexBuffer()->UnBind();

		if (pMtx)
			pObject->SetPrevMatrix(*pMtx);
	}

	_motionBlurProgram->UnBind();
	mpLowLevelGraphics->SetTexture(0, NULL);
	mpLowLevelGraphics->SetTexture(1, NULL);

	pCam->SetPrevView(pCam->GetViewMatrix());
	pCam->SetPrevProjection(pCam->GetProjectionMatrix());
}

//-----------------------------------------------------------------------

void cRendererPostEffects::RenderBloom() {
	if (mbBloomActive == false)
		return;

	if (!_bloomProgram)
		return;

	//////////////////////////////
	iTexture *pScreenTexture = mpScreenBuffer[mImageTrailData.mlCurrentBuffer == 0 ? 1 : 0];

	mpLowLevelGraphics->CopyContextToTexure(pScreenTexture, 0,
											cVector2l((int)mvScreenSize.x, (int)mvScreenSize.y));

	RenderBlurTexture(mpBloomBlurTexture, pScreenTexture, mfBloomSpread);

	/*cVector2f vBlurSize = */ cVector2f((float)mpBloomBlurTexture->getWidth(), (float)mpBloomBlurTexture->getHeight());

	cVector2f vVirtSize = mpLowLevelGraphics->GetVirtualSize();

	///////////////////////////////////////////
	// Draw Bloom

	_bloomProgram->Bind();
	_bloomProgram->SetMatrixf("worldViewProj", eGpuProgramMatrix_ViewProjection, eGpuProgramMatrixOp_Identity);

	mpLowLevelGraphics->SetTexture(0, mpBloomBlurTexture);
	mpLowLevelGraphics->SetTexture(1, pScreenTexture);

	{
		tVector3fVec vUvVec;
		vUvVec.resize(4);

		vUvVec[0] = cVector2f(0, mvScreenSize.y);
		vUvVec[1] = cVector2f(mvScreenSize.x, mvScreenSize.y);
		vUvVec[2] = cVector2f(mvScreenSize.x, 0);
		vUvVec[3] = cVector2f(0, 0);

		mvTexRectVtx[0] = cVertex(cVector3f(0, 0, 40), cVector2f(0, 1), cColor(1, 1.0f));
		mvTexRectVtx[1] = cVertex(cVector3f(vVirtSize.x, 0, 40), cVector2f(1, 1), cColor(1, 1.0f));
		mvTexRectVtx[2] = cVertex(cVector3f(vVirtSize.x, vVirtSize.y, 40), cVector2f(1, 0), cColor(1, 1.0f));
		mvTexRectVtx[3] = cVertex(cVector3f(0, vVirtSize.y, 40), cVector2f(0, 0), cColor(1, 1.0f));

		mpLowLevelGraphics->DrawQuadMultiTex(mvTexRectVtx, vUvVec);
	}

	_bloomProgram->UnBind();

	mpLowLevelGraphics->SetTexture(0, NULL);
	mpLowLevelGraphics->SetTexture(1, NULL);
}

//-----------------------------------------------------------------------

void cRendererPostEffects::RenderImageTrail() {
	if (mImageTrailData.mbActive == false)
		return;

	tVertexVec vVtx;
	vVtx.push_back(cVertex(cVector3f(0, 0, 40), cVector2f(0, mvScreenSize.y), cColor(1, 0.6f)));
	vVtx.push_back(cVertex(cVector3f(800, 0, 40), cVector2f(mvScreenSize.x, mvScreenSize.y), cColor(1, 0.6f)));
	vVtx.push_back(cVertex(cVector3f(800, 600, 40), cVector2f(mvScreenSize.x, 0), cColor(1, 0.6f)));
	vVtx.push_back(cVertex(cVector3f(0, 600, 40), cVector2f(0, 0), cColor(1, 0.6f)));

	mpLowLevelGraphics->CopyContextToTexure(mpScreenBuffer[mImageTrailData.mlCurrentBuffer == 0 ? 1 : 0], 0, cVector2l((int)mpLowLevelGraphics->GetScreenSize().x, (int)mpLowLevelGraphics->GetScreenSize().y));
	{
		if (mImageTrailData.mbActive) {
			if (mImageTrailData.mbFirstPass) {
				mpLowLevelGraphics->SetBlendActive(false);
				mpLowLevelGraphics->SetTexture(0, mpScreenBuffer[mImageTrailData.mlCurrentBuffer == 0 ? 1 : 0]);
			} else {

				mpLowLevelGraphics->SetClearDepthActive(false);
				mpLowLevelGraphics->SetClearColor(cColor(0, 0));
				mpLowLevelGraphics->ClearScreen();
				mpLowLevelGraphics->SetClearDepthActive(true);

				mpLowLevelGraphics->SetBlendActive(true);
				mpLowLevelGraphics->SetBlendFunc(eBlendFunc_SrcAlpha, eBlendFunc_Zero);

				mpLowLevelGraphics->SetTexture(0, mpScreenBuffer[mImageTrailData.mlCurrentBuffer == 0 ? 1 : 0]);

				mpLowLevelGraphics->SetTextureConstantColor(cColor(1, 1.0f - mImageTrailData.mfAmount));
				mpLowLevelGraphics->SetTextureEnv(eTextureParam_AlphaSource0, eTextureSource_Constant);
				mpLowLevelGraphics->SetTextureEnv(eTextureParam_AlphaFunc, eTextureFunc_Replace);
			}
		} else {
			mpLowLevelGraphics->SetBlendActive(false);
			mpLowLevelGraphics->SetTexture(0, mpScreenBuffer[mImageTrailData.mlCurrentBuffer == 0 ? 1 : 0]);
		}

		mpLowLevelGraphics->DrawQuad(vVtx);

		if (mImageTrailData.mbActive) {
			if (!mImageTrailData.mbFirstPass) {
				mpLowLevelGraphics->SetBlendFunc(eBlendFunc_SrcAlpha, eBlendFunc_One);

				mpLowLevelGraphics->SetTexture(0, mpScreenBuffer[mImageTrailData.mlCurrentBuffer]);

				mpLowLevelGraphics->SetTextureConstantColor(cColor(1, mImageTrailData.mfAmount));
				mpLowLevelGraphics->SetTextureEnv(eTextureParam_AlphaSource0, eTextureSource_Constant);
				mpLowLevelGraphics->SetTextureEnv(eTextureParam_AlphaFunc, eTextureFunc_Replace);

				mpLowLevelGraphics->DrawQuad(vVtx);
			} else {
				mImageTrailData.mbFirstPass = false;
			}
			mImageTrailData.mlCurrentBuffer = mImageTrailData.mlCurrentBuffer == 0 ? 1 : 0;

			mpLowLevelGraphics->CopyContextToTexure(mpScreenBuffer[mImageTrailData.mlCurrentBuffer], 0, cVector2l((int)mpLowLevelGraphics->GetScreenSize().x, (int)mpLowLevelGraphics->GetScreenSize().y));
		}

		mpLowLevelGraphics->SetTextureEnv(eTextureParam_AlphaSource0, eTextureSource_Texture);
		mpLowLevelGraphics->SetTextureEnv(eTextureParam_AlphaSource1, eTextureSource_Previous);
		mpLowLevelGraphics->SetTextureEnv(eTextureParam_AlphaFunc, eTextureFunc_Modulate);
		mpLowLevelGraphics->SetTextureEnv(eTextureParam_ColorSource0, eTextureSource_Texture);
		mpLowLevelGraphics->SetTextureEnv(eTextureParam_ColorSource1, eTextureSource_Previous);
		mpLowLevelGraphics->SetTextureEnv(eTextureParam_ColorFunc, eTextureFunc_Modulate);

		mpLowLevelGraphics->SetBlendActive(false);
		mpLowLevelGraphics->SetTexture(0, NULL);
	}
}

//-----------------------------------------------------------------------

} // namespace hpl
