/*
 * Copyright (C) 2020 - lewa_j
 *
 * This file is part of HPL1 Engine.
 *
 * HPL1 Engine is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HPL1 Engine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HPL1 Engine.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "hpl1/engine/impl/LowLevelGraphicsAndroid.h"
#include "hpl1/engine/graphics/font_data.h"

#include "hpl1/engine/graphics/bitmap2D.h"
#include "hpl1/engine/graphics/font_data.h"
#include "hpl1/engine/impl/CGProgram.h"
#include "hpl1/engine/impl/AndroidTexture.h"
// #include "hpl1/engine/impl/VertexBufferOGL.h"
// #include "hpl1/engine/impl/VertexBufferVBO.h"
#include "hpl1/engine/impl/VertexBufferGLES.h"
#include "hpl1/engine/math/Math.h"
#include "hpl1/engine/system/low_level_system.h"

#include "common/algorithm.h"
#include "common/config-manager.h"
#include "common/system.h"
#include "engines/util.h"
#include "hpl1/debug.h"
#include "hpl1/engine/impl/OcclusionQueryOGL.h"
#include "hpl1/graphics.h"

namespace hpl
{



GLenum ColorFormatToGL(eColorDataFormat aFormat) {
	switch(aFormat)
		{
		case eColorDataFormat_RGB:		return GL_RGB;
		case eColorDataFormat_RGBA:		return GL_RGBA;
		case eColorDataFormat_ALPHA:	return GL_ALPHA;
//		case eColorDataFormat_BGR:		return GL_BGR;
		case eColorDataFormat_BGRA:		return GL_BGRA;

	default:
		break;
	}
	Hpl1::logError(Hpl1::kDebugOpenGL, "invalid color format (%d)\n", aFormat);
	return 0;
}

GLenum TextureTargetToGL(eTextureTarget target) {
	switch (target) {
	case eTextureTarget_1D:
	case eTextureTarget_2D:
		return GL_TEXTURE_2D;

	case eTextureTarget_Rect:
	case eTextureTarget_CubeMap:
	case eTextureTarget_3D:
	case eTextureTarget_LastEnum:
		break;
	}
	Hpl1::logError(Hpl1::kDebugOpenGL, "invalid texture target (%d)\n", target);
	return 0;
}


GLenum cLowLevelGraphicsAndroid::GetGLBlendEnum(eBlendFunc type) {
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



//-----------------------------------------------------------------------

GLenum cLowLevelGraphicsAndroid::GetGLTextureOpEnum(eTextureOp type) {
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

GLenum cLowLevelGraphicsAndroid::GetGLTextureSourceEnum(eTextureSource type) {
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



//-----------------------------------------------------------------------
GLenum cLowLevelGraphicsAndroid::GetGLDepthTestFuncEnum(eDepthTestFunc type) {
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

GLenum cLowLevelGraphicsAndroid::GetGLAlphaTestFuncEnum(eAlphaTestFunc type) {
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
GLenum cLowLevelGraphicsAndroid::GetGLTextureTargetEnum(eTextureTarget aType)
	{
		switch (aType)
		{
	//	case eTextureTarget_1D:		return GL_TEXTURE_1D;
		case eTextureTarget_2D:		return GL_TEXTURE_2D;
	//	case eTextureTarget_Rect:	return GL_TEXTURE_RECTANGLE;
		case eTextureTarget_CubeMap:	return GL_TEXTURE_CUBE_MAP;
		case eTextureTarget_3D:		return GL_TEXTURE_3D;
		
		default:
		break;
	}
	Hpl1::logError(Hpl1::kDebugOpenGL, "invalid GetGLTextureTargetEnum (%d)", aType);
	
		return 0;
	}
//-----------------------------------------------------------------------

GLenum cLowLevelGraphicsAndroid::GetGLStencilFuncEnum(eStencilFunc type) {
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

GLenum cLowLevelGraphicsAndroid::GetGLStencilOpEnum(eStencilOp type) {
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


	cLowLevelGraphicsAndroid::cLowLevelGraphicsAndroid()
	{		
		mlBatchArraySize = 20000;
		mlVertexCount = 0;
		mlIndexCount = 0;
		mlMultisampling = 0;

		mvVirtualSize.x = 800;
		mvVirtualSize.y = 600;

		mpRenderTarget = nullptr;
		mpPixelFormat = Graphics::PixelFormat::createFormatRGBA32();
		for(int i=0;i<MAX_TEXTUREUNITS;i++)
			mpCurrentTexture[i] = nullptr;

		for(int i=0;i<eMatrix_LastEnum;i++)
			mMatrixStack[i].push(cMatrixf::Identity);

		mbClearColor = true;
		mbClearDepth = true;
		mbClearStencil = false;

		//Create the batch arrays:
		mlBatchStride = 13;
		//3 Pos floats, 4 color floats, 3 Tex coord floats .
		mpVertexArray = (float*)malloc(sizeof(float) * mlBatchStride * mlBatchArraySize);
		mpIndexArray = (unsigned int*)malloc(sizeof(unsigned int) * mlBatchArraySize); //Index is one int.

		for(int i=0;i<MAX_TEXTUREUNITS;i++)
		{
			mpTexCoordArray[i] = (float*)malloc(sizeof(float) * 3 * mlBatchArraySize);
			mbTexCoordArrayActive[i] = false;
			mlTexCoordArrayCount[i]=0;
		}
	}
	static const bool egl_log = true;
	cLowLevelGraphicsAndroid::~cLowLevelGraphicsAndroid()
	{
		free(mpVertexArray);
		free(mpIndexArray);
		for(int i=0;i<MAX_TEXTUREUNITS;i++)
			free(mpTexCoordArray[i]);
	}
	
	bool cLowLevelGraphicsAndroid::Init(int alWidth, int alHeight, int alBpp, int abFullscreen,
									int alMultisampling, const tString& asWindowCaption)
	{
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
		//Turn off cursor as default
		ShowCursor(false);

		//GL
		Log(" Setting up OpenGL\n");
		SetupGL();

		//Set the clear color
		//eglSwapBuffers(mEglDisplay,mEglSurface);
			g_system->updateScreen();

	/*_gammaCorrectionProgram = CreateGpuProgram("hpl1_gamma_correction", "hpl1_gamma_correction");
	_screenBuffer = CreateTexture(cVector2l(
									  (int)mvScreenSize.x, (int)mvScreenSize.y),
								  32, cColor(0, 0, 0, 0), false,
								  eTextureType_Normal, eTextureTarget_Rect);
*/
mSimpleShader =  CreateGpuProgram("hpl1_gamma_correction", "hpl1_gamma_correction");

		mSimpleShader->Bind();

		mDefaultTexture = CreateTexture({1,1},32,cColor(1,1),false,eTextureType_Normal,eTextureTarget_2D);

		return true;
	}
	
	void cLowLevelGraphicsAndroid::SetupGL()
	{
		GL_CHECK(glViewport(0, 0, mvScreenSize.x, mvScreenSize.y));
		// Inits GL stuff
		// Set Shade model and clear color.
		//GL_CHECK(glShadeModel(GL_SMOOTH));
		GL_CHECK(glClearColor(0.0f, 0.0f, 0.0f, 0.0f));

		//Depth Test setup
		glClearDepthf(1.0f);//Values buffer is cleared with
		glEnable(GL_DEPTH_TEST); //enable depth testing
		glDepthFunc(GL_LEQUAL); //function to do depth test with
		//glDisable(GL_ALPHA_TEST);

		//Set best perspective correction
		//glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

		//Stencil setup
		glClearStencil(0);

		//Clear the screen
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

		/////  BEGIN BATCH ARRAY STUFF ///////////////
/*
		//Enable all the vertex arrays that are used:
		glEnableClientState(GL_VERTEX_ARRAY ); //The positions
		glEnableClientState(GL_COLOR_ARRAY ); //The color
		glEnableClientState(GL_TEXTURE_COORD_ARRAY); //Tex coords
		glDisableClientState(GL_NORMAL_ARRAY);
		//Disable the once not used.
		glDisableClientState(GL_INDEX_ARRAY); //color index
		glDisableClientState(GL_EDGE_FLAG_ARRAY);
*/
		///// END BATCH ARRAY STUFF ///////////////



		//Show some info
		Log("  Max texture image units: %d\n",GetCaps(eGraphicCaps_MaxTextureImageUnits));
		Log("  Max texture coord units: %d\n",GetCaps(eGraphicCaps_MaxTextureCoordUnits));

		Log("  Anisotropic filtering: %d\n",GetCaps(eGraphicCaps_AnisotropicFiltering));
		if(GetCaps(eGraphicCaps_AnisotropicFiltering))
			Log("  Max Anisotropic degree: %d\n",GetCaps(eGraphicCaps_MaxAnisotropicFiltering));

		
	}
	//-----------------------------------------------------------------------

	int cLowLevelGraphicsAndroid::GetCaps(eGraphicCaps aType) const
	{
		switch(aType)
		{
		//Texture Rectangle
		case eGraphicCaps_TextureTargetRectangle:
			{
				return 0;//ARB_texture_rectangle
			}

		//Vertex Buffer Object
		case eGraphicCaps_VertexBufferObject:
			{
				return 1;
			}

		//Two Sided Stencil
		case eGraphicCaps_TwoSideStencil:
			{
				return 1;
			}

		//Max Texture Image Units
		case eGraphicCaps_MaxTextureImageUnits:
			{
				//DEBUG:
				//return 2;

				int lUnits;
				glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,(GLint *)&lUnits);
				return lUnits;
			}

		//Max Texture Coord Units
		case eGraphicCaps_MaxTextureCoordUnits:
			{
				int lUnits;
				glGetIntegerv(GL_MAX_VERTEX_ATTRIBS,(GLint *)&lUnits);
				return lUnits;
			}
		//Texture Anisotropy
		case eGraphicCaps_AnisotropicFiltering:
			{
				if(GL_EXT_texture_filter_anisotropic) return 1;
				else return 0;
			}

		//Texture Anisotropy
		case eGraphicCaps_MaxAnisotropicFiltering:
			{
				if(!GL_EXT_texture_filter_anisotropic) return 0;

				float fMax;
				glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT,&fMax);
				return (int)fMax;
			}

		//Multisampling
		case eGraphicCaps_Multisampling:
			{
				return 1;
			}

		//GL Vertex program
		case eGraphicCaps_GL_VertexProgram:
			{
				//Debbug:
				//return 0;
				return 1;
			}

		//GL Fragment program
		case eGraphicCaps_GL_FragmentProgram:
			{
				//Debbug:
				//return 0;
				return 1;
			}
		
		//GL NV register combiners
		case eGraphicCaps_GL_NVRegisterCombiners:
			{
				return 0;
			}

		//GL NV register combiners Max stages
		case eGraphicCaps_GL_NVRegisterCombiners_MaxStages:
			{
				return 0;
			}

		//GL ATI Fragment Shader
		case eGraphicCaps_GL_ATIFragmentShader:
			{
				return 0;
			}

		default:
			return 0;
		}

		return 0;
	}
	/*
	void cLowLevelGraphicsAndroid::SetVsyncActive(bool abX)
	{
		mSwapInterval = abX ? 1 : 0;
		eglSwapInterval(mEglDisplay, mSwapInterval);
	}*/
	void cLowLevelGraphicsAndroid::SetMultisamplingActive(bool abX)
	{
		//There is no glEnable(GL_MULTISAMPLE) in gles2
	}
	void cLowLevelGraphicsAndroid::SetGammaCorrection(float afX)
	{
		
	}
	float cLowLevelGraphicsAndroid::GetGammaCorrection()
	{
		return 1;
	}
	
	cVector2f cLowLevelGraphicsAndroid::GetScreenSize()
	{
		return cVector2f((float)mvScreenSize.x, (float)mvScreenSize.y);
	}

	//-----------------------------------------------------------------------

	cVector2f cLowLevelGraphicsAndroid::GetVirtualSize()
	{
		return mvVirtualSize;
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::SetVirtualSize(cVector2f avSize)
	{
		mvVirtualSize = avSize;
	}

	//-----------------------------------------------------------------------
	
	Bitmap2D* cLowLevelGraphicsAndroid::CreateBitmap2D(const cVector2l &avSize)
	{

		Bitmap2D *pBmp = hplNew( Bitmap2D, (avSize, mpPixelFormat) );
		pBmp->create(avSize, mpPixelFormat);
		return pBmp;
	}
	

	
	FontData* cLowLevelGraphicsAndroid::CreateFontData(const tString &asName)
	{
		Hpl1::logInfo(Hpl1::kDebugGraphics,"Creating font data %s", asName.c_str());
		return hplNew(FontData, (asName, this));
	}
	
	iTexture* cLowLevelGraphicsAndroid::CreateTexture(bool abUseMipMaps, eTextureType aType, eTextureTarget aTarget)
	{
		return hplNew( cAndroidTexture, ("",&mpPixelFormat,this,aType, abUseMipMaps, aTarget) );
	}
	
	iTexture* cLowLevelGraphicsAndroid::CreateTexture(const tString &asName,bool abUseMipMaps, eTextureType aType, eTextureTarget aTarget)
	{
		return hplNew( cAndroidTexture, (asName,&mpPixelFormat,this,aType, abUseMipMaps, aTarget) );
	}
	
	iTexture* cLowLevelGraphicsAndroid::CreateTexture(Bitmap2D* apBmp,bool abUseMipMaps, eTextureType aType, eTextureTarget aTarget)
	{
		cAndroidTexture *pTex = hplNew( cAndroidTexture, ("",&mpPixelFormat,this,aType, abUseMipMaps, aTarget) );
		pTex->CreateFromBitmap(apBmp);
		return pTex;
	}
	
	iTexture* cLowLevelGraphicsAndroid::CreateTexture(const cVector2l& avSize,int alBpp,cColor aFillCol,
							bool abUseMipMaps, eTextureType aType, eTextureTarget aTarget)
	{
		cAndroidTexture *pTex=NULL;

		if(aType==eTextureType_RenderTarget)
		{
			pTex = hplNew( cAndroidTexture, ("",&mpPixelFormat,this,aType, abUseMipMaps, aTarget) );
			pTex->Create(avSize.x, avSize.y, aFillCol);
		}
		else
		{
			Bitmap2D* pBmp = CreateBitmap2D(avSize);
			pBmp->fillRect(cRect2l(0,0,0,0),aFillCol);

			pTex = hplNew( cAndroidTexture, ("",&mpPixelFormat,this,aType, abUseMipMaps, aTarget) );
			bool bRet = pTex->CreateFromBitmap(pBmp);
			
			hplDelete(pBmp);

			if(bRet==false){
				hplDelete(pTex);
				return NULL;
			}
		}
		return pTex;
	}

	iGpuProgram *cLowLevelGraphicsAndroid::CreateGpuProgram(const tString &vertex, const tString &fragment) {
		return hplNew(cCGProgram, (vertex, fragment));
	}
	
	iVertexBuffer* cLowLevelGraphicsAndroid::CreateVertexBuffer(tVertexFlag aFlags, eVertexBufferDrawType aDrawType,
										eVertexBufferUsageType aUsageType,
										int alReserveVtxSize,int alReserveIdxSize)
	{
		return hplNew( cVertexBufferGLES, (this, aFlags,aDrawType,aUsageType,alReserveVtxSize,alReserveIdxSize) );
	}

	//-----------------------------------------------------------------------

	class cOcclusionQueryNull : public iOcclusionQuery
	{
	public:
		cOcclusionQueryNull(){}
		~cOcclusionQueryNull(){}

		void Begin(){}
		void End(){}
		bool FetchResults(){return true;}
		unsigned int GetSampleCount(){return 100;}
	};

	iOcclusionQuery* cLowLevelGraphicsAndroid::CreateOcclusionQuery()
	{
		return hplNew(cOcclusionQueryNull, () );
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::DestroyOcclusionQuery(iOcclusionQuery *apQuery)
	{
		if(apQuery)	hplDelete(apQuery);
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::FlushRendering()
	{
		glFlush();
	}
	void cLowLevelGraphicsAndroid::SwapBuffers()
	{
		glFlush();
	//	eglSwapBuffers(mEglDisplay, mEglSurface);
	SetBlendActive(false);

	// Copy screen to texture
	CopyContextToTexure(mDefaultTexture, 0,
						cVector2l((int)mvScreenSize.x, (int)mvScreenSize.y));

	SetTexture(0, mDefaultTexture);
	g_system->updateScreen();
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::PushMatrix(eMatrix aMtxType)
	{
		mMatrixStack[aMtxType].push(mMatrixStack[aMtxType].top());
	}

	void cLowLevelGraphicsAndroid::PopMatrix(eMatrix aMtxType)
	{
		mMatrixStack[aMtxType].pop();
	}

	void cLowLevelGraphicsAndroid::SetMatrix(eMatrix aMtxType, const cMatrixf& a_mtxA)
	{
		mMatrixStack[aMtxType].top() = a_mtxA;

		UploadShaderMatrix();
	}

	void cLowLevelGraphicsAndroid::SetIdentityMatrix(eMatrix aMtxType)
	{
		mMatrixStack[aMtxType].top() = cMatrixf::Identity;

		UploadShaderMatrix();
	}

	void cLowLevelGraphicsAndroid::TranslateMatrix(eMatrix aMtxType, const cVector3f &avPos)
	{
		cMatrixf &r = mMatrixStack[aMtxType].top();
		cMatrixf m = r;
		for(int i=0;i<4;i++)
			r.m[i][3] = m.m[i][0] * avPos.x + m.m[i][1] * avPos.y + m.m[i][2] * avPos.z + m.m[i][3];
	}

	/**
	 * \todo fix so that there are X, Y , Z versions of this one.
	 * \param aMtxType
	 * \param &avRot
	 */
	void cLowLevelGraphicsAndroid::RotateMatrix(eMatrix aMtxType, const cVector3f &avRot)
	{
		//TODO its unused anyway
		//mMatrixStack[aMtxType].top().Rotate(avRot);
	}

	void cLowLevelGraphicsAndroid::ScaleMatrix(eMatrix aMtxType, const cVector3f &avScale)
	{
		VEC3_CONST_ARRAY(vel, avScale);
		cMatrixf &r = mMatrixStack[aMtxType].top();
		cMatrixf m = r;
		for(int i=0;i<3;i++)
			for(int j=0;j<4;j++)
				r.m[j][i] = m.m[j][i] * vel[i];
	}

	void cLowLevelGraphicsAndroid::SetOrthoProjection(const cVector2f& avSize, float afMin, float afMax)
	{
		cMatrixf &r = mMatrixStack[eMatrix_Projection].top();
		r = cMatrixf::Identity;
		r.m[0][0] = 2.0f / avSize.x;
		r.m[1][1] = 2.0f / -avSize.y;
		r.m[2][2] = -2.0f / (afMax - afMin);
		r.m[0][3] = -1.0f;
		r.m[1][3] = 1.0f;
		r.m[2][3] = - (afMax + afMin) / (afMax - afMin);
		r.m[3][3] = 1;
	}

	void cLowLevelGraphicsAndroid::ClearScreen()
	{
		GLbitfield bitmask=0;

		if(mbClearColor)bitmask |= GL_COLOR_BUFFER_BIT;
		if(mbClearDepth)bitmask |= GL_DEPTH_BUFFER_BIT;
		if(mbClearStencil)bitmask |= GL_STENCIL_BUFFER_BIT;

		glClear(bitmask);
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::SetClearColor(const cColor& aCol)
	{
		glClearColor(aCol.r, aCol.g, aCol.b, aCol.a);
	}

	void cLowLevelGraphicsAndroid::SetClearDepth(float afDepth)
	{
		glClearDepthf(afDepth);
	}

	void cLowLevelGraphicsAndroid::SetClearStencil(int alVal)
	{
		glClearStencil(alVal);
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::SetClearColorActive(bool abX)
	{
		mbClearColor=abX;
	}

	void cLowLevelGraphicsAndroid::SetClearDepthActive(bool abX)
	{
		mbClearDepth=abX;
	}

	void cLowLevelGraphicsAndroid::SetClearStencilActive(bool abX)
	{
		mbClearStencil=abX;
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::SetColorWriteActive(bool abR,bool abG,bool abB,bool abA)
	{
		glColorMask(abR,abG,abB,abA);
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::SetDepthWriteActive(bool abX)
	{
		glDepthMask(abX);
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::SetDepthTestActive(bool abX)
	{
		if(abX) glEnable(GL_DEPTH_TEST);
		else glDisable(GL_DEPTH_TEST);
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::SetDepthTestFunc(eDepthTestFunc aFunc)
	{
		glDepthFunc(GetGLDepthTestFuncEnum(aFunc));
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::SetAlphaTestActive(bool abX)
	{
		//TODO QCOM extension and shader emulation
		//if(abX) glEnable(GL_ALPHA_TEST);
		//else glDisable(GL_ALPHA_TEST);
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::SetAlphaTestFunc(eAlphaTestFunc aFunc,float afRef)
	{
		//glAlphaFunc(GetGLAlphaTestFuncEnum(aFunc),afRef);
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::SetStencilActive(bool abX)
	{
		if(abX) glEnable(GL_STENCIL_TEST);
		else glDisable(GL_STENCIL_TEST);
	}
	
	void cLowLevelGraphicsAndroid::SetStencil(eStencilFunc aFunc,int alRef, unsigned int aMask,
					eStencilOp aFailOp,eStencilOp aZFailOp,eStencilOp aZPassOp)
	{
		glStencilFunc(GetGLStencilFuncEnum(aFunc), alRef, aMask);

		glStencilOp(GetGLStencilOpEnum(aFailOp), GetGLStencilOpEnum(aZFailOp),
					GetGLStencilOpEnum(aZPassOp));
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::SetStencilTwoSide(eStencilFunc aFrontFunc,eStencilFunc aBackFunc,
					int alRef, unsigned int aMask,
					eStencilOp aFrontFailOp,eStencilOp aFrontZFailOp,eStencilOp aFrontZPassOp,
					eStencilOp aBackFailOp,eStencilOp aBackZFailOp,eStencilOp aBackZPassOp)
	{
		//Front
		glStencilOpSeparate( GL_FRONT, GetGLStencilOpEnum(aFrontFailOp),
								GetGLStencilOpEnum(aFrontZFailOp),
								GetGLStencilOpEnum(aFrontZPassOp));
		//Back
		glStencilOpSeparate( GL_BACK, GetGLStencilOpEnum(aBackFailOp),
								GetGLStencilOpEnum(aBackZFailOp),
								GetGLStencilOpEnum(aBackZPassOp));

		glStencilFuncSeparate( GL_FRONT, GetGLStencilFuncEnum(aFrontFunc),
								alRef, aMask);
		glStencilFuncSeparate( GL_BACK, GetGLStencilFuncEnum(aBackFunc),
								alRef, aMask);
	}

	void cLowLevelGraphicsAndroid::SetStencilTwoSide(bool abX)
	{
		//glDisable(GL_STENCIL_TEST_TWO_SIDE_EXT);
		//in gles2 it's always on
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::SetCullActive(bool abX)
	{
		if(abX) glEnable(GL_CULL_FACE);
		else glDisable(GL_CULL_FACE);
		glCullFace(GL_BACK);
	}
	void cLowLevelGraphicsAndroid::SetCullMode(eCullMode aMode)
	{
		glCullFace(GL_BACK);
		if(aMode == eCullMode_Clockwise)
			glFrontFace(GL_CCW);
		else
			glFrontFace(GL_CW);
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::SetScissorActive(bool abX)
	{
		if(abX) glEnable(GL_SCISSOR_TEST);
		else glDisable(GL_SCISSOR_TEST);
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::SetScissorRect(const cRect2l &aRect)
	{
		glScissor(aRect.x, (mvScreenSize.y - aRect.y - 1)-aRect.h, aRect.w, aRect.h);
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::SetBlendActive(bool abX)
	{
		if(abX)
			glEnable(GL_BLEND);
		else
			glDisable(GL_BLEND);
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::SetBlendFunc(eBlendFunc aSrcFactor, eBlendFunc aDestFactor)
	{
		glBlendFunc(GetGLBlendEnum(aSrcFactor),GetGLBlendEnum(aDestFactor));
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::SetBlendFuncSeparate(eBlendFunc aSrcFactorColor, eBlendFunc aDestFactorColor,
		eBlendFunc aSrcFactorAlpha, eBlendFunc aDestFactorAlpha)
	{
		glBlendFuncSeparate(GetGLBlendEnum(aSrcFactorColor),
								GetGLBlendEnum(aDestFactorColor),
								GetGLBlendEnum(aSrcFactorAlpha),
								GetGLBlendEnum(aDestFactorAlpha));
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::SetTexture(unsigned int alUnit,iTexture* apTex)
	{
		if(apTex == NULL){
			apTex = mDefaultTexture;
		}

		if(apTex == mpCurrentTexture[alUnit])
			return;

		GLenum NewTarget = 0;
		if(apTex)
			NewTarget = GetGLTextureTargetEnum(apTex->GetTarget());
		GLenum LastTarget = 0;
		if(mpCurrentTexture[alUnit])
			LastTarget = GetGLTextureTargetEnum(mpCurrentTexture[alUnit]->GetTarget());

		glActiveTexture(GL_TEXTURE0 + alUnit);

		glBindTexture(NewTarget, apTex->GetCurrentLowlevelHandle());

		mpCurrentTexture[alUnit] = apTex;
	}

	void cLowLevelGraphicsAndroid::SetActiveTextureUnit(unsigned int alUnit)
	{
		glActiveTexture(GL_TEXTURE0 + alUnit);
	}

	void cLowLevelGraphicsAndroid::SetTextureEnv(eTextureParam aParam, int alVal)
	{

	}

	void cLowLevelGraphicsAndroid::SetTextureConstantColor(const cColor &aColor)
	{

	}

	void cLowLevelGraphicsAndroid::SetColor(const cColor &aColor)
	{

	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::DrawQuad(const tVertexVec &avVtx)
	{
		assert(avVtx.size()==4);

		glVertexAttribPointer(eVtxAttr_Position, 3, GL_FLOAT, false, sizeof(cVertex), &avVtx[0].pos.x);
		glVertexAttribPointer(eVtxAttr_Color0, 4, GL_FLOAT, false, sizeof(cVertex), &avVtx[0].col.r);
		glVertexAttribPointer(eVtxAttr_Texture0, 3, GL_FLOAT, false, sizeof(cVertex), &avVtx[0].tex.x);

		uint16_t quadIndices[]{0,1,2,2,3,0};
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, quadIndices);
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::AddVertexToBatch(const cVertex &apVtx)
	{
		//Coord
		mpVertexArray[mlVertexCount + 0] =	apVtx.pos.x;
		mpVertexArray[mlVertexCount + 1] =	apVtx.pos.y;
		mpVertexArray[mlVertexCount + 2] =	apVtx.pos.z;
		//Color
		mpVertexArray[mlVertexCount + 3] =	apVtx.col.r;
		mpVertexArray[mlVertexCount + 4] =	apVtx.col.g;
		mpVertexArray[mlVertexCount + 5] =	apVtx.col.b;
		mpVertexArray[mlVertexCount + 6] =	apVtx.col.a;
		//Texture coord
		mpVertexArray[mlVertexCount + 7] =	apVtx.tex.x;
		mpVertexArray[mlVertexCount + 8] =	apVtx.tex.y;
		mpVertexArray[mlVertexCount + 9] =	apVtx.tex.z;
		//Normal coord
		mpVertexArray[mlVertexCount + 10] =	apVtx.norm.x;
		mpVertexArray[mlVertexCount + 11] =	apVtx.norm.y;
		mpVertexArray[mlVertexCount + 12] =	apVtx.norm.z;

		mlVertexCount += mlBatchStride;

		if(mlVertexCount/mlBatchStride >= mlBatchArraySize)
		{
			//Make the array larger.
		}
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::AddVertexToBatch(const cVertex *apVtx, const cVector3f* avTransform)
	{
		//Coord
		mpVertexArray[mlVertexCount + 0] =	apVtx->pos.x+avTransform->x;
		mpVertexArray[mlVertexCount + 1] =	apVtx->pos.y+avTransform->y;
		mpVertexArray[mlVertexCount + 2] =	apVtx->pos.z+avTransform->z;
		//Color
		mpVertexArray[mlVertexCount + 3] =	apVtx->col.r;
		mpVertexArray[mlVertexCount + 4] =	apVtx->col.g;
		mpVertexArray[mlVertexCount + 5] =	apVtx->col.b;
		mpVertexArray[mlVertexCount + 6] =	apVtx->col.a;
		//Texture coord
		mpVertexArray[mlVertexCount + 7] =	apVtx->tex.x;
		mpVertexArray[mlVertexCount + 8] =	apVtx->tex.y;
		mpVertexArray[mlVertexCount + 9] =	apVtx->tex.z;
		//Normal coord
		mpVertexArray[mlVertexCount + 10] =	apVtx->norm.x;
		mpVertexArray[mlVertexCount + 11] =	apVtx->norm.y;
		mpVertexArray[mlVertexCount + 12] =	apVtx->norm.z;

		mlVertexCount += mlBatchStride;

		if(mlVertexCount/mlBatchStride >= mlBatchArraySize)
		{
			//Make the array larger.
		}
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::AddVertexToBatch_Size2D(const cVertex *apVtx, const cVector3f* avTransform,
		const cColor* apCol,const float& mfW, const float& mfH)
	{
		//Coord
		mpVertexArray[mlVertexCount + 0] =	avTransform->x + mfW;
		mpVertexArray[mlVertexCount + 1] =	avTransform->y + mfH;
		mpVertexArray[mlVertexCount + 2] =	avTransform->z;
		//Color
		mpVertexArray[mlVertexCount + 3] =	apCol->r;
		mpVertexArray[mlVertexCount + 4] =	apCol->g;
		mpVertexArray[mlVertexCount + 5] =	apCol->b;
		mpVertexArray[mlVertexCount + 6] =	apCol->a;
		//Texture coord
		mpVertexArray[mlVertexCount + 7] =	apVtx->tex.x;
		mpVertexArray[mlVertexCount + 8] =	apVtx->tex.y;
		mpVertexArray[mlVertexCount + 9] =	apVtx->tex.z;

		mlVertexCount += mlBatchStride;

		if(mlVertexCount/mlBatchStride >= mlBatchArraySize)
		{
			//Make the array larger.
		}
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::AddVertexToBatch_Raw(	const cVector3f& avPos, const cColor &aColor,
														const cVector3f& avTex)
	{
		//Coord
		mpVertexArray[mlVertexCount + 0] =	avPos.x;
		mpVertexArray[mlVertexCount + 1] =	avPos.y;
		mpVertexArray[mlVertexCount + 2] =	avPos.z;
		//Color
		mpVertexArray[mlVertexCount + 3] =	aColor.r;
		mpVertexArray[mlVertexCount + 4] =	aColor.g;
		mpVertexArray[mlVertexCount + 5] =	aColor.b;
		mpVertexArray[mlVertexCount + 6] =	aColor.a;
		//Texture coord
		mpVertexArray[mlVertexCount + 7] =	avTex.x;
		mpVertexArray[mlVertexCount + 8] =	avTex.y;
		mpVertexArray[mlVertexCount + 9] =	avTex.z;

		mlVertexCount += mlBatchStride;
	}


	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::AddIndexToBatch(int alIndex)
	{
		mpIndexArray[mlIndexCount] = alIndex;
		mlIndexCount++;

		if(mlIndexCount>=mlBatchArraySize)
		{
			//Make the array larger.
		}
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::FlushTriBatch(tVtxBatchFlag aTypeFlags, bool abAutoClear)
	{
		mSimpleShader->Bind();
		UploadShaderMatrix();

		SetVtxBatchStates(aTypeFlags);
		SetUpBatchArrays();

		glDrawElements(GL_TRIANGLES,mlIndexCount,GL_UNSIGNED_INT, mpIndexArray);

		if(abAutoClear){
			mlIndexCount = 0;
			mlVertexCount = 0;
			for(int i=0;i<MAX_TEXTUREUNITS;i++)
				mlTexCoordArrayCount[i]=0;
		}
	}

	void cLowLevelGraphicsAndroid::FlushQuadBatch(tVtxBatchFlag aTypeFlags, bool abAutoClear) {
		SetVtxBatchStates(aTypeFlags);
		SetUpBatchArrays();
		//GL_CHECK(glDrawElements(GL_QUADS, mlIndexCount, GL_UNSIGNED_INT, mpIndexArray));
		if (abAutoClear){
			//flushAutoClear(mlIndexCount, mlVertexCount, mlTexCoordArrayCount);

			mlIndexCount = 0;
			mlVertexCount = 0;
			Common::fill(mlTexCoordArrayCount, mlTexCoordArrayCount + MAX_TEXTUREUNITS, 0);
		}
	}




	void cLowLevelGraphicsAndroid::ClearBatch()
	{
		mlIndexCount = 0;
		mlVertexCount = 0;
	}
	
	//-----------------------------------------------------------------------
	
	void cLowLevelGraphicsAndroid::SetUpBatchArrays()
	{
		//Set the arrays
		glVertexAttribPointer(eVtxAttr_Position, 3, GL_FLOAT, false, sizeof(float)*mlBatchStride, mpVertexArray);
		glVertexAttribPointer(eVtxAttr_Color0, 4, GL_FLOAT, false, sizeof(float)*mlBatchStride, &mpVertexArray[3]);
		glVertexAttribPointer(eVtxAttr_Normal, 3, GL_FLOAT, false, sizeof(float)*mlBatchStride, &mpVertexArray[10]);

		glVertexAttribPointer(eVtxAttr_Texture0, 3, GL_FLOAT, false, sizeof(float)*mlBatchStride, &mpVertexArray[7]);
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::SetVtxBatchStates(tVtxBatchFlag aFlags)
	{
		if(aFlags & eVtxBatchFlag_Position)
			glEnableVertexAttribArray( eVtxAttr_Position );
		else glDisableVertexAttribArray( eVtxAttr_Position );

		if(aFlags & eVtxBatchFlag_Color0)
			glEnableVertexAttribArray( eVtxAttr_Color0 );
		else glDisableVertexAttribArray( eVtxAttr_Color0 );

		if(aFlags & eVtxBatchFlag_Normal)
			glEnableVertexAttribArray( eVtxAttr_Normal );
		else glDisableVertexAttribArray( eVtxAttr_Normal );

		if(aFlags & eVtxBatchFlag_Texture0)
			glEnableVertexAttribArray( eVtxAttr_Texture0 );
		else glDisableVertexAttribArray( eVtxAttr_Texture0 );
	}

	//-----------------------------------------------------------------------

	void cLowLevelGraphicsAndroid::UploadShaderMatrix()
	{
		cMatrixf mtx = cMath::MatrixMul(mMatrixStack[eMatrix_Projection].top(),mMatrixStack[eMatrix_ModelView].top());
		mSimpleShader->SetMatrixf("worldViewProj",mtx);
	}

}

