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

#include "common/scummsys.h"

// RiscOS uses its own plugin provider and SDL one doesn't work
#if defined(DYNAMIC_MODULES) && defined(SDL_BACKEND)

#include "backends/plugins/sdl/sdl-provider.h"
#include "backends/plugins/dynamic-plugin.h"
#include "common/fs.h"

#include "common/file.h"
#include "backends/platform/sdl/sdl-sys.h"

class SDLPlugin : public DynamicPlugin {
protected:
	SDL_SharedObject *_dlHandle;

	virtual VoidFunc findSymbol(const char *symbol) {
		SDL_FunctionPointer func = SDL_LoadFunction(_dlHandle, symbol);
		if (!func)
			warning("Failed loading symbol '%s' from plugin '%s' (%s)", symbol, _filename.toString(Common::Path::kNativeSeparator).c_str(), SDL_GetError());

		// FIXME HACK: This is a HACK to circumvent a clash between the ISO C++
		// standard and POSIX: ISO C++ disallows casting between function pointers
		// and data pointers, but dlsym always returns a void pointer. For details,
		// see e.g. <http://www.trilithium.com/johan/2004/12/problem-with-dlsym/>.
		assert(sizeof(VoidFunc) == sizeof(func));
		VoidFunc tmp;
		memcpy(&tmp, &func, sizeof(VoidFunc));
		return tmp;
	}

public:
	SDLPlugin(const Common::Path &filename)
		: DynamicPlugin(filename), _dlHandle(0) {}

	bool loadPlugin() {
		assert(!_dlHandle);
		Common::String fsCachePath = Common::normalizePath("/.cache/" + _filename.toString(Common::Path::kNativeSeparator), '/');
		/* load file just for the heck of it*/
	Common::File file;
		Common::FSNode node(_filename);
	file.open(node);
	if (!file.isOpen()) {
		warning("Could not open file %s!", _filename.toString(Common::Path::kNativeSeparator).c_str());
		return false;
	}
	const int32 size = file.size();
	char *bytes = new char[size + 1];
	file.read(bytes, size);
	file.close();
	warning("File %s loaded, should now be avaialble at %s", _filename.toString(Common::Path::kNativeSeparator).c_str(), fsCachePath.c_str());
		_dlHandle = SDL_LoadObject(fsCachePath.c_str());

		if (!_dlHandle) {
			warning("Failed loading plugin '%s' (%s)", _filename.toString(Common::Path::kNativeSeparator).c_str(), SDL_GetError());
			return false;
		}

		return DynamicPlugin::loadPlugin();
	}

	void unloadPlugin() {
		DynamicPlugin::unloadPlugin();
		if (_dlHandle) {
			SDL_UnloadObject(_dlHandle);
			_dlHandle = 0;
		}
	}
};


Plugin* SDLPluginProvider::createPlugin(const Common::FSNode &node) const {
	return new SDLPlugin(node.getPath());
}


#endif // defined(DYNAMIC_MODULES) && defined(SDL_BACKEND)
