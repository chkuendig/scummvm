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

#ifndef HPL1_OPENGL_H
#define HPL1_OPENGL_H

#include "common/ptr.h"
#include "common/scummsys.h"
#include "graphics/opengl/context.h"
#include "graphics/opengl/system_headers.h"
#include "graphics/opengl/debug.h" // for OPENGL_DEBUG (disabled on __EMSCRIPTEN__)

#if defined(USE_OPENGL_GAME) || USE_FORCED_GLES2

#define HPL1_USE_OPENGL

namespace Graphics {

struct Surface;

}

namespace Hpl1 {

void checkOGLErrors(const char *function, const char *file, int line);

Common::ScopedPtr<Graphics::Surface> createGLViewportScreenshot();

} // namespace Hpl1

// checkOGLErrors() calls glGetError(), which on WebGL forces a synchronous
// CPU<->GPU round-trip. Doing that after every GL call (220+ sites, incl. the
// per-frame draw path) dominated CPU on the Emscripten target. Gate it behind
// the shared OPENGL_DEBUG flag (graphics/opengl/debug.h), which is disabled for
// __EMSCRIPTEN__, so release/web builds skip the per-call check - matching the
// shared GL backend's GL_CALL policy.
#ifdef OPENGL_DEBUG
#define GL_CHECK(x)                                           \
	{                                                         \
		x;                                                    \
		::Hpl1::checkOGLErrors(__func__, __FILE__, __LINE__); \
	}
#define GL_CHECK_FN() \
	{ ::Hpl1::checkOGLErrors(__func__, __FILE__, __LINE__); }
#else
#define GL_CHECK(x) \
	{ x; }
#define GL_CHECK_FN() \
	{}
#endif

#endif // defined(USE_OPENGL_GAME) || USE_FORCED_GLES2
#endif // HPL1_OPENGL_H
