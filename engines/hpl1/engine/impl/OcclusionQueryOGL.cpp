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

#include "hpl1/engine/impl/OcclusionQueryOGL.h"

#ifdef HPL1_USE_OPENGL

// OpenGL ES compatibility for occlusion queries
#ifdef USE_FORCED_GLES2
// OpenGL ES 2.0 doesn't have occlusion queries, provide dummy constants
#ifndef GL_SAMPLES_PASSED_EXT
#define GL_SAMPLES_PASSED_EXT 0x8914
#endif
#ifndef GL_QUERY_RESULT_EXT
#define GL_QUERY_RESULT_EXT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE_EXT
#define GL_QUERY_RESULT_AVAILABLE_EXT 0x8867
#endif

// Define dummy function prototypes (will be no-ops)
#ifdef USE_FORCED_GLES2
// OpenGL ES 2.0 has these as extension functions, but we'll provide no-op implementations
// since occlusion queries aren't critical for basic functionality
#define glGenQueriesEXT(n, ids) do { (void)(n); (void)(ids); } while(0)
#define glDeleteQueriesEXT(n, ids) do { (void)(n); (void)(ids); } while(0)
#define glBeginQueryEXT(target, id) do { (void)(target); (void)(id); } while(0)
#define glEndQueryEXT(target) do { (void)(target); } while(0)
#define glGetQueryObjectivEXT(id, pname, params) do { \
    (void)(id); (void)(pname); \
    if (params) *(params) = 1; \
} while(0)
#else
// Desktop OpenGL - provide dummy implementations if extensions not available
static void glGenQueriesEXT(GLsizei n, GLuint* ids) { (void)n; (void)ids; }
static void glDeleteQueriesEXT(GLsizei n, const GLuint* ids) { (void)n; (void)ids; }
static void glBeginQueryEXT(GLenum target, GLuint id) { (void)target; (void)id; }
static void glEndQueryEXT(GLenum target) { (void)target; }
static void glGetQueryObjectivEXT(GLuint id, GLenum pname, GLint* params) { 
    (void)id; (void)pname; 
    if (params) *params = 1; // Always return 1 sample for compatibility
}
#endif

#ifndef GL_SAMPLES_PASSED
#define GL_SAMPLES_PASSED GL_SAMPLES_PASSED_EXT
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT GL_QUERY_RESULT_EXT
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE GL_QUERY_RESULT_AVAILABLE_EXT
#endif
#define glGenQueries glGenQueriesEXT
#define glDeleteQueries glDeleteQueriesEXT
#define glBeginQuery glBeginQueryEXT
#define glEndQuery glEndQueryEXT
#define glGetQueryObjectiv glGetQueryObjectivEXT
#endif // USE_FORCED_GLES2

namespace hpl {

//////////////////////////////////////////////////////////////////////////
// CONSTRUCTORS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

cOcclusionQueryOGL::cOcclusionQueryOGL() {
	glGenQueries(1, (GLuint *)&mlQueryId);
	mlLastSampleCount = 0;
}

//-----------------------------------------------------------------------

cOcclusionQueryOGL::~cOcclusionQueryOGL() {
	glDeleteQueries(1, (GLuint *)&mlQueryId);
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

void cOcclusionQueryOGL::Begin() {
	glBeginQuery(GL_SAMPLES_PASSED, mlQueryId);
}

void cOcclusionQueryOGL::End() {
	glEndQuery(GL_SAMPLES_PASSED);
}

bool cOcclusionQueryOGL::FetchResults() {
	int lAvailable = 0;
	glGetQueryObjectiv(mlQueryId, GL_QUERY_RESULT_AVAILABLE, (GLint *)&lAvailable);
	if (lAvailable == 0)
		return false;

	glGetQueryObjectiv(mlQueryId, GL_QUERY_RESULT, (GLint *)&mlLastSampleCount);
	return true;
}

unsigned int cOcclusionQueryOGL::GetSampleCount() {
	return mlLastSampleCount;
}

//-----------------------------------------------------------------------
} // namespace hpl

#endif // HL1_USE_OPENGL
