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
#include "AndroidTexture.h"
#include "hpl1/opengl.h"

#include "hpl1/debug.h"
#include "hpl1/engine/graphics/bitmap2D.h"
#include "hpl1/engine/math/Math.h"
#include "hpl1/engine/system/low_level_system.h"

#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R 0x8072
#endif
namespace hpl
{

	static GLenum GetGLWrapEnum(eTextureWrap aMode) {
		switch (aMode) {
		case eTextureWrap_Clamp:
			return GL_CLAMP_TO_EDGE;  // GL_CLAMP not available in GLES2
		case eTextureWrap_Repeat:
			return GL_REPEAT;
		case eTextureWrap_ClampToEdge:
			return GL_CLAMP_TO_EDGE;
		case eTextureWrap_ClampToBorder:
			return GL_CLAMP_TO_EDGE;  // GL_CLAMP_TO_BORDER not available in GLES2
		default:
			break;
		}
		Hpl1::logError(Hpl1::kDebugOpenGL, "invalid TextureWrap  (%04X)\n", aMode);
	
		return GL_REPEAT;
	}

	GLenum GetGLTextureTargetEnum(eTextureTarget aType)
	{
		switch (aType)
		{
		//case eTextureTarget_1D:		return GL_TEXTURE_1D;
		case eTextureTarget_2D:		return GL_TEXTURE_2D;
		//case eTextureTarget_Rect:	return GL_TEXTURE_RECTANGLE;
		case eTextureTarget_CubeMap:	return GL_TEXTURE_CUBE_MAP;
		case eTextureTarget_3D:		return GL_TEXTURE_3D;
		
			default:
		break;
	}
	Hpl1::logError(Hpl1::kDebugOpenGL, "invalid TextureTarget  (0x%0.2X)\n", aType);
		return 0;
	}
	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	cAndroidTexture::cAndroidTexture(const tString &asName,Graphics::PixelFormat *apPxlFmt,iLowLevelGraphics* apLowLevelGraphics,
				eTextureType aType, bool abUseMipMaps, eTextureTarget aTarget,
				bool abCompress)
	: iTexture(asName,"GL",apPxlFmt,apLowLevelGraphics,aType,abUseMipMaps, aTarget, abCompress)
	{
		if(mTarget == eTextureTarget_1D)
			mTarget = eTextureTarget_2D;
		mbContainsData = false;

		//Cubemap does not like mipmaps
		if(aTarget == eTextureTarget_CubeMap) mbUseMipMaps = false;

		mlTextureIndex = 0;
		mfTimeCount =0;

		mfTimeDir = 1;

		//_bpp = 0;
	}

	cAndroidTexture::~cAndroidTexture()
	{
		for(size_t i=0; i<mvTextureHandles.size(); ++i)
		{
			glDeleteTextures(1,(GLuint *)&mvTextureHandles[i]);
		}
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	bool cAndroidTexture::CreateFromBitmap(Bitmap2D* pBmp)
	{
		//Generate handles
		if(mvTextureHandles.empty())
		{
			mvTextureHandles.resize(1);
			glGenTextures(1,(GLuint *)&mvTextureHandles[0]);
		}

		return CreateFromBitmapToHandle(pBmp,0);
	}

	//-----------------------------------------------------------------------

	bool cAndroidTexture::CreateAnimFromBitmapVec(tBitmap2DVec *avBitmaps)
	{
		mvTextureHandles.resize(avBitmaps->size());

		for(size_t i=0; i< mvTextureHandles.size(); ++i)
		{
			glGenTextures(1,(GLuint *)&mvTextureHandles[i]);
			if(CreateFromBitmapToHandle((*avBitmaps)[i],(int)i)==false)
			{
				return false;
			}
		}

		return true;
	}

	//-----------------------------------------------------------------------

	bool cAndroidTexture::CreateCubeFromBitmapVec(tBitmap2DVec *avBitmaps)
	{
		if(mType == eTextureType_RenderTarget || mTarget != eTextureTarget_CubeMap)
		{
			return false;
		}

		if(avBitmaps->size()<6){
			Hpl1::logError(Hpl1::kDebugTextures, "Only %d bitmaps supplied for creation of cube map, 6 needed.", avBitmaps->size());
			return false;
		}

		//Generate handles
		if(mvTextureHandles.empty())
		{
			mvTextureHandles.resize(1);
			glGenTextures(1,(GLuint *)&mvTextureHandles[0]);
		}
		else
		{
			glDeleteTextures(1,(GLuint *)&mvTextureHandles[0]);
			glGenTextures(1,(GLuint *)&mvTextureHandles[0]);
		}

		GLenum GLTarget = InitCreation(0);

		//Create the cube map sides
		for(int i=0; i< 6; i++)
		{
			Bitmap2D *pSrc = (*avBitmaps)[i];

			GLenum target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + i;

			int lChannels = pSrc->getNumChannels();
			GLenum format = lChannels==4 ? GL_RGBA : GL_RGB;

			glTexImage2D(target, 0, format, pSrc->getWidth(), pSrc->getHeight(),
				0, format, GL_UNSIGNED_BYTE, pSrc->getRawData());

			_width = pSrc->getWidth();
			_height = pSrc->getHeight();
			_bpp = lChannels * 8;

			if(!cMath::IsPow2(_height) || !cMath::IsPow2(_width))
			{
				Hpl1::logWarning(Hpl1::kDebugTextures, "Texture '%s' does not have a pow2 size", msName.c_str());
			}
		}

		PostCreation(GLTarget);

		return true;
	}

	bool cAndroidTexture::Create(unsigned int alWidth, unsigned int alHeight, cColor aCol)
	{
		error("call to unimplemented function AndroidTexture::Create");
		return true;
	}

	bool cAndroidTexture::CreateFromArray(unsigned char *apPixelData, int alChannels, const cVector3l &avSize)
	{
		if(mvTextureHandles.empty())
		{
			mvTextureHandles.resize(1);
			glGenTextures(1,(GLuint *)&mvTextureHandles[0]);
		}

		GLenum GLTarget = InitCreation(0);

		GLenum format = 0;
		switch(alChannels)
		{
		case 1: format = GL_LUMINANCE; break;
		case 2: format = GL_LUMINANCE_ALPHA; break;
		case 3: format = GL_RGB; break;
		case 4: format = GL_RGBA; break;
		}

		_width = avSize.x;
		_height = avSize.y;
	//	mlDepth = avSize.z;
		_bpp = alChannels * 8;

		if(!cMath::IsPow2(_height) || !cMath::IsPow2(_width) || !cMath::IsPow2(avSize.z))
		{
			Hpl1::logWarning(Hpl1::kDebugTextures, "Texture '%s' does not have a pow2 size", msName.c_str());
		}

		if(mTarget == eTextureTarget_2D)
		{
			glTexImage2D(GLTarget, 0, format, _width, _height,
						0, format, GL_UNSIGNED_BYTE, apPixelData);
		}
		else if(mTarget == eTextureTarget_3D)
		{

			Hpl1::logWarning(Hpl1::kDebugTextures, "Texture '%s' unsupported; eTextureTarget_3D", msName.c_str());
		/*	glTexImage3D(GLTarget, 0, format, avSize.x, avSize.y,avSize.z,
				0, format, GL_UNSIGNED_BYTE, apPixelData);*/
		}

		if(mbUseMipMaps && mTarget != eTextureTarget_Rect && mTarget != eTextureTarget_3D)
		{
			glGenerateMipmap(GLTarget);
		}

		PostCreation(GLTarget);

		return true;
	}

	//-----------------------------------------------------------------------

	void cAndroidTexture::SetPixels2D(	int alLevel, const cVector2l& avOffset, const cVector2l& avSize,
									eColorDataFormat aDataFormat, void *apPixelData)
	{
		if(mTarget != eTextureTarget_2D && mTarget != eTextureTarget_Rect) return;

		glTexSubImage2D(GetGLTextureTargetEnum(mTarget),alLevel,avOffset.x,avOffset.y, avSize.x,avSize.y,
						ColorFormatToGL(aDataFormat),GL_UNSIGNED_BYTE,apPixelData);
	}

	//-----------------------------------------------------------------------

	void cAndroidTexture::Update(float afTimeStep)
	{
		if(mvTextureHandles.size() > 1)
		{
			float fMax = (float)(mvTextureHandles.size());
			mfTimeCount += afTimeStep * (1.0f/mfFrameTime) * mfTimeDir;

			if(mfTimeDir > 0)
			{
				if(mfTimeCount >= fMax)
				{
					if(mAnimMode == eTextureAnimMode_Loop)
					{
						mfTimeCount =0;
					}
					else
					{
						mfTimeCount = fMax - 1.0f;
						mfTimeDir = -1.0f;
					}
				}
			}
			else
			{
				if(mfTimeCount < 0)
				{
					mfTimeCount =1;
					mfTimeDir = 1.0f;
				}
			}
		}
	}

	bool cAndroidTexture::HasAnimation()
	{
		return mvTextureHandles.size() > 1;
	}

	void cAndroidTexture::NextFrame()
	{
		mfTimeCount += mfTimeDir;

		if(mfTimeDir > 0)
		{
			float fMax = (float)(mvTextureHandles.size());
			if(mfTimeCount >= fMax)
			{
				if(mAnimMode == eTextureAnimMode_Loop)
				{
					mfTimeCount =0;
				}
				else
				{
					mfTimeCount = fMax - 1.0f;
					mfTimeDir = -1.0f;
				}
			}
		}
		else
		{
			if(mfTimeCount < 0)
			{
				mfTimeCount =1;
				mfTimeDir = 1.0f;
			}
		}
	}

	void cAndroidTexture::PrevFrame()
	{
		mfTimeCount -= mfTimeDir;

		if(mfTimeDir < 0)
		{
			float fMax = (float)(mvTextureHandles.size());
			if(mfTimeCount >= fMax)
			{
				if(mAnimMode == eTextureAnimMode_Loop)
				{
					mfTimeCount =0;
				}
				else
				{
					mfTimeCount = fMax - 1.0f;
					mfTimeDir = -1.0f;
				}
			}
		}
		else
		{
			if(mfTimeCount < 0)
			{
				mfTimeCount =1;
				mfTimeDir = 1.0f;
			}
		}
	}

	float cAndroidTexture::GetT()
	{
		return cMath::Modulus(mfTimeCount,1.0f);
	}

	float cAndroidTexture::GetTimeCount()
	{
		return mfTimeCount;
	}

	void cAndroidTexture::SetTimeCount(float afX)
	{
		mfTimeCount = afX;
	}

	int cAndroidTexture::GetCurrentLowlevelHandle()
	{
		return GetTextureHandle();
	}

	//-----------------------------------------------------------------------

	void cAndroidTexture::SetFilter(eTextureFilter aFilter)
	{
		if(mFilter == aFilter) return;

		mFilter = aFilter;
		if(mbContainsData)
		{
			GLenum GLTarget = GetGLTextureTargetEnum(mTarget);

			for(size_t i=0; i < mvTextureHandles.size(); ++i)
			{
				glBindTexture(GLTarget, mvTextureHandles[i]);

				if(mbUseMipMaps && mTarget != eTextureTarget_Rect){
					if(mFilter == eTextureFilter_Bilinear)
						glTexParameteri(GLTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
					else
						glTexParameteri(GLTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
				}
				else{
					glTexParameteri(GLTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				}
			}
		}
	}

	void cAndroidTexture::SetAnisotropyDegree(float afX)
	{
		if(!mpLowLevelGraphics->GetCaps(eGraphicCaps_AnisotropicFiltering)) return;
		if(afX < 1.0f) return;
		if(afX > (float) mpLowLevelGraphics->GetCaps(eGraphicCaps_MaxAnisotropicFiltering)) return;

		if(mfAnisotropyDegree == afX) return;

		mfAnisotropyDegree = afX;

		GLenum GLTarget = GetGLTextureTargetEnum(mTarget);

		for(size_t i=0; i < mvTextureHandles.size(); ++i)
		{
			glBindTexture(GLTarget, mvTextureHandles[i]);

			glTexParameterf(GLTarget,GL_TEXTURE_MAX_ANISOTROPY_EXT ,mfAnisotropyDegree);
		}
	}

	void cAndroidTexture::SetWrapS(eTextureWrap aMode)
	{
		if(mbContainsData){
			GLenum GLTarget = GetGLTextureTargetEnum(mTarget);

Hpl1::logInfo(Hpl1::kDebugTextures, "setting texture '%s' s wrap to %d, target %04X\n", msName.c_str(), aMode	, GLTarget);

			for(size_t i=0; i < mvTextureHandles.size(); ++i){
				glBindTexture(GLTarget, mvTextureHandles[i]);
				glTexParameteri(GLTarget,GL_TEXTURE_WRAP_S,GetGLWrapEnum(aMode));
			}
					GL_CHECK_FN();
		}
	}

	void cAndroidTexture::SetWrapT(eTextureWrap aMode)
	{
		if(mbContainsData){

		
			GLenum GLTarget = GetGLTextureTargetEnum(mTarget);
Hpl1::logInfo(Hpl1::kDebugTextures, "setting texture '%s' t wrap to %d, target %04X\n", msName.c_str(), aMode	, GLTarget);

			for(size_t i=0; i < mvTextureHandles.size(); ++i){
				glBindTexture(GLTarget, mvTextureHandles[i]);
				glTexParameteri(GLTarget,GL_TEXTURE_WRAP_T,GetGLWrapEnum(aMode));
			}
		}
	}

	void cAndroidTexture::SetWrapR(eTextureWrap aMode)
	{
	
		if(mbContainsData){
			GLenum GLTarget = GetGLTextureTargetEnum(mTarget);
				GL_CHECK(glEnable(GLTarget));
		glEnable(GLTarget);
	
			for(size_t i=0; i < mvTextureHandles.size(); ++i){
				glBindTexture(GLTarget, mvTextureHandles[i]);
				glTexParameteri(GLTarget,GL_TEXTURE_WRAP_R,GetGLWrapEnum(aMode));
			}
					GL_CHECK(glDisable(GLTarget));

		glDisable(GLTarget);
		}
	}

	//-----------------------------------------------------------------------

	unsigned int cAndroidTexture::GetTextureHandle()
	{
		if(mvTextureHandles.size() > 1)
		{
			int lFrame = (int) mfTimeCount;
			return mvTextureHandles[lFrame];
		}
		else
		{
			return mvTextureHandles[0];
		}
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PRIVATE METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	bool cAndroidTexture::CreateFromBitmapToHandle(Bitmap2D* pBmp, int alHandleIdx)
	{
		if(mType == eTextureType_RenderTarget)
		{
			error("trying to create a rendertarget in AndroidTexture::CreateBitmapToHandle");
			return false;
		}

		GLenum GLTarget = InitCreation(alHandleIdx);

		_width = pBmp->getWidth();
		_height = pBmp->getHeight();

		if((!cMath::IsPow2(_height) || !cMath::IsPow2(_width)) && mTarget != eTextureTarget_Rect)
		{
			Hpl1::logWarning(Hpl1::kDebugTextures, "Texture '%s' does not have a pow2 size", msName.c_str());
		}

		int lChannels = pBmp->getNumChannels();
		GLenum format = lChannels==4 ? GL_RGBA : GL_RGB;

		_bpp = lChannels * 8;

		unsigned char *pPixelSrc = (unsigned char*)pBmp->getRawData();

		unsigned char *pNewSrc = NULL;
		if(mlSizeLevel>0 && (int)_width > mvMinLevelSize.x*2)
		{
			//Log("OldSize: %d x %d ",_width,_height);

			int lOldW = _width;
			int lOldH = _height;

			int lSizeDiv = (int)pow((float)2,(int)mlSizeLevel);

			_width /= lSizeDiv;
			_height /= lSizeDiv;

			while(_width < (unsigned int)mvMinLevelSize.x)
			{
				_width*=2;
				_height*=2;
				lSizeDiv/=2;
			}

			//Log("NewSize: %d x %d SizeDiv: %d\n",_width,_height,lSizeDiv);

			pNewSrc = hplNewArray( unsigned char, lChannels * _width * _height);

			int lWidthCount = _width;
			int lHeightCount = _height;
			int lOldAdd = lChannels*lSizeDiv;
			int lOldHeightAdd = lChannels*lOldW*(lSizeDiv-1);

			unsigned char *pOldPixel = pPixelSrc;
			unsigned char *pNewPixel = pNewSrc;

			while(lHeightCount)
			{
				memcpy(pNewPixel, pOldPixel,lChannels);

				pOldPixel += lOldAdd;
				pNewPixel += lChannels;

				lWidthCount--;
				if(!lWidthCount)
				{
					lWidthCount = _width;
					lHeightCount--;
					pOldPixel += lOldHeightAdd;
				}
			}

			pPixelSrc = pNewSrc;
		}

		//Log("Loading %s  %d x %d\n",msName.c_str(), pSrc->GetWidth(), pSrc->GetHeight());
		//Log("Channels: %d Format: %x\n",lChannels, format);

		//Clear error flags
		while(glGetError()!=GL_NO_ERROR);

		glTexImage2D(GLTarget, 0, format, _width, _height,
			0, format, GL_UNSIGNED_BYTE, pPixelSrc);

		if(glGetError() != GL_NO_ERROR){
			Hpl1::logWarning(Hpl1::kDebugTextures, "glError after glTexImage2D");
			return false;
		}

		if(mbUseMipMaps && mTarget != eTextureTarget_Rect)
		{
			glGenerateMipmap(GLTarget);
		}

		PostCreation(GLTarget);

		if(mlSizeLevel>0 && pNewSrc)
		{
			hplDeleteArray(pNewSrc);
		}

		return true;
	}

	//-----------------------------------------------------------------------

	unsigned int cAndroidTexture::InitCreation(int alHandleIdx)
	{
		GLenum GLTarget = GetGLTextureTargetEnum(mTarget);
		glBindTexture(GLTarget, mvTextureHandles[alHandleIdx]);

		return GLTarget;
	}

	//-----------------------------------------------------------------------

	void cAndroidTexture::PostCreation(GLenum aGLTarget)
	{
		if(mbUseMipMaps && mTarget != eTextureTarget_Rect)
		{
			if(mFilter == eTextureFilter_Bilinear)
				glTexParameteri(aGLTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
			else
				glTexParameteri(aGLTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		}
		else{
			glTexParameteri(aGLTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		}
		glTexParameteri(aGLTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(aGLTarget,GL_TEXTURE_WRAP_S,GL_REPEAT);
		glTexParameteri(aGLTarget,GL_TEXTURE_WRAP_T,GL_REPEAT);
		//glTexParameteri(aGLTarget,GL_TEXTURE_WRAP_R,GL_REPEAT);

		mbContainsData = true;
	}

	//-----------------------------------------------------------------------

}
