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

#include "hpl1/engine/impl/GLSLProgram.h"
#include "hpl1/engine/impl/LowLevelGraphicsAndroid.h"

#include "hpl1/engine/math/Math.h"
#include "hpl1/engine/system/String.h"
#include "hpl1/engine/scene/Camera3D.h"

#include "common/array.h"
#include "common/str.h"
#include "hpl1/debug.h"
#include "math/matrix4.h"

namespace hpl
{

static OpenGL::Shader *createShader(const char *vertex, const char *fragment) {
	const char *attributes[] = {nullptr};
	return OpenGL::Shader::fromFiles(vertex, fragment, attributes);
}

cGLSLProgram::cGLSLProgram(const tString &vertex, const tString &fragment)
	: iGpuProgram(vertex + " " + fragment) {

		/*
		glBindAttribLocation(mId,eVtxAttr_Position,"a_position");
	glBindAttribLocation(mId,eVtxAttr_Color0,"a_color");
	glBindAttribLocation(mId,eVtxAttr_Normal,"a_normal");
	glBindAttribLocation(mId,eVtxAttr_Texture0,"a_uv");
	glBindAttribLocation(mId,eVtxAttr_Tangent,"a_tangent");
		
		*/
	char *kAttributes[6] = {nullptr};
	kAttributes[eVtxAttr_Position] = (char *)"a_position";
	kAttributes[eVtxAttr_Color0] = (char *)"a_color";
	kAttributes[eVtxAttr_Normal] = (char *)"a_normal";
	kAttributes[eVtxAttr_Texture0] = (char *)"a_uv";
	kAttributes[eVtxAttr_Tangent] = (char *)"a_tangent";
	_shader = OpenGL::Shader::fromFiles(vertex.c_str(), fragment.c_str(), kAttributes);

/* glUseProgram(mId);
	glUniform1i(glGetUniformLocation(mId,"diffuseMap"),0);
	glUniform1i(glGetUniformLocation(mId,"normalMap"),1);
	glUniform1i(glGetUniformLocation(mId,"normalCubeMap"),2);
	glUniform1i(glGetUniformLocation(mId,"falloffMap"),3);
	glUniform1i(glGetUniformLocation(mId,"spotlightMap"),4);
	glUniform1i(glGetUniformLocation(mId,"spotNegRejectMap"),5);
	glUniform1i(glGetUniformLocation(mId,"specularMap"),6);
	glUseProgram(0);
	*/
	_shader->use();
	_shader->setUniform("diffuseMap",0);
	_shader->setUniform("normalMap",1);
	_shader->setUniform("normalCubeMap",2);
	_shader->setUniform("falloffMap",3);
	_shader->setUniform("spotlightMap",4);
	_shader->setUniform("spotNegRejectMap",5);
	_shader->setUniform("specularMap",6);

	_shader->unbind();
	
}
cGLSLProgram::~cGLSLProgram()
{
	if(mId)
		glDeleteProgram(mId);
}

bool cGLSLProgram::reload()
{
	return false;
}
void cGLSLProgram::unload()
{
	
}
void cGLSLProgram::destroy()
{
	
}

tString cGLSLProgram::GetProgramName()
{
	return msName;
}

/*
bool cGLSLProgram::CreateFromFile(const tString& asFile, const tString& asEntry)
{
	return false;
}

char* LoadCharBuffer(const tString& asFileName, int& alLength)
{
	FILE *pFile = fopen(asFileName.c_str(), "rb");
	if(pFile == nullptr){
		alLength = 0;
		return nullptr;
	}

	int lLength = (int)Platform::FileLength(pFile);
	alLength = lLength;

	char *pBuffer = hplNewArray(char, lLength+1);
	fread(pBuffer, lLength, 1, pFile);
	pBuffer[lLength] = 0;

	fclose(pFile);

	return pBuffer;
}
bool cGLSLProgram::CreateFromFiles(const tString& asFileVertex, const tString& asFileFragment)
{
//	if(mProgramType != eGpuProgramType_Linked)
//		return false;

	// TODO: create shader from files
	//	const char *attributes[] = {nullptr};
	//return OpenGL::Shader::fromFiles(vertex, fragment, attributes);
	int vsl,fsl;
	char *vs = LoadCharBuffer(asFileVertex, vsl);
	char *fs = LoadCharBuffer(asFileFragment, fsl);
	bool cr = Create(vs, fs);
	hplDeleteArray(vs);
	hplDeleteArray(fs);
	if(!cr){
		return false;
	}

	return true;
}
*/
//-----------------------------------------------------------------------

void cGLSLProgram::Bind() {
	Hpl1::logInfo(Hpl1::kDebugShaders, "binding shader %s\n", GetProgramName().c_str());
	_shader->use();
}

//-----------------------------------------------------------------------

void cGLSLProgram::UnBind() {
	Hpl1::logInfo(Hpl1::kDebugShaders, "unbinding shader %s\n", GetProgramName().c_str());
	_shader->unbind();
}

bool cGLSLProgram::SetFloat(const tString &asName, float afX) {
	_shader->setUniform1f(asName.c_str(), afX);
	return true;
}

//-----------------------------------------------------------------------

bool cGLSLProgram::SetVec2f(const tString &asName, float afX, float afY) {
	_shader->setUniform(asName.c_str(), {afX, afY});
	return true;
}

//-----------------------------------------------------------------------

bool cGLSLProgram::SetVec3f(const tString &asName, float afX, float afY, float afZ) {
	_shader->setUniform(asName.c_str(), {afX, afY, afZ});
	return true;
}

//-----------------------------------------------------------------------

bool cGLSLProgram::SetVec4f(const tString &asName, float afX, float afY, float afZ, float afW) {
	_shader->setUniform(asName.c_str(), {afX, afY, afZ, afW});
	return true;
}

//-----------------------------------------------------------------------

bool cGLSLProgram::SetMatrixf(const tString &asName, const cMatrixf &mMtx) {
	Math::Matrix4 mat4;
	mat4.setData(mMtx.v);
	mat4.transpose();
	_shader->setUniform(asName.c_str(), mat4);
	return true;
}

bool cGLSLProgram::SetMatrixf(const tString& asName, eGpuProgramMatrix mType,
							eGpuProgramMatrixOp mOp)
{
	Warning("cGLSLProgram::SetMatrixf unimplemented type %d op %d\n",mType, mOp);
	return false;
}

bool cGLSLProgram::SetTexture(const tString& asName,iTexture* apTexture, bool abAutoDisable)
{
	Warning("cGLSLProgram::SetTexture unimplemented\n");
	return false;
}

bool cGLSLProgram::SetTextureToUnit(int alUnit, iTexture* apTexture)
{
	Warning("cGLSLProgram::SetTextureToUnit unimplemented\n");
	return false;
}

int CreateShader(int type, const char *src)
{
	int s = glCreateShader(type);
	glShaderSource(s, 1, &src, 0);
	glCompileShader(s);
	int status = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &status);
	if(!status){
		Log("%s shader compile status %d\n",type==GL_VERTEX_SHADER ? "Vertex" : "Fragment", status);
		
		int size = 0;
		glGetShaderiv(s, GL_INFO_LOG_LENGTH, &size);
		char data[size];
		glGetShaderInfoLog(s, size, &size, data);
		
		Log("Compile log:\n%s\n===========\n",data);
	}
	return s;
}

bool cGLSLProgram::Create(const char *vt, const char *ft)
{
	int vs = CreateShader(GL_VERTEX_SHADER,vt);
	int fs = CreateShader(GL_FRAGMENT_SHADER,ft);

	mId = glCreateProgram();
	glBindAttribLocation(mId,eVtxAttr_Position,"a_position");
	glBindAttribLocation(mId,eVtxAttr_Color0,"a_color");
	glBindAttribLocation(mId,eVtxAttr_Normal,"a_normal");
	glBindAttribLocation(mId,eVtxAttr_Texture0,"a_uv");
	glBindAttribLocation(mId,eVtxAttr_Tangent,"a_tangent");

	glAttachShader(mId,vs);
	glAttachShader(mId,fs);
	glLinkProgram(mId);

	glDeleteShader(vs);
	glDeleteShader(fs);

	int status = 0;
	glGetProgramiv(mId,GL_LINK_STATUS,&status);
	if(!status)
		Log("link status %d\n",status);

	int size=0;
	glGetProgramiv(mId, GL_INFO_LOG_LENGTH, &size);
	if(size){
		char data[size];
		glGetProgramInfoLog(mId, size, &size, data);
		Log("Shader program log: %s\n",data);
	}

	if(!status)
		return false;

	glUseProgram(mId);
	glUniform1i(glGetUniformLocation(mId,"diffuseMap"),0);
	glUniform1i(glGetUniformLocation(mId,"normalMap"),1);
	glUniform1i(glGetUniformLocation(mId,"normalCubeMap"),2);
	glUniform1i(glGetUniformLocation(mId,"falloffMap"),3);
	glUniform1i(glGetUniformLocation(mId,"spotlightMap"),4);
	glUniform1i(glGetUniformLocation(mId,"spotNegRejectMap"),5);
	glUniform1i(glGetUniformLocation(mId,"specularMap"),6);
	glUseProgram(0);

	return status;
}

}
