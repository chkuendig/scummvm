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

#ifndef PLATFORM_SDL_EMSCRIPTEN_H
#define PLATFORM_SDL_EMSCRIPTEN_H

#include "backends/platform/sdl/posix/posix.h"

#ifdef USE_CLOUD
#include "backends/networking/curl/request.h"
#include "common/ustr.h"

typedef Common::BaseCallback<const Common::String *> *CloudConnectionCallback;
#endif

extern "C" {
void clipboard_paste_callback(char *paste_data); // pass clipboard paste data from JS to backend
void cloud_connection_json_callback(char *str);       // pass cloud storage activation data from JS to setup wizard
void store_file(char const *filename, char const *mime_type, char *buffer, size_t buffer_size); // upload file to home direcory storage

}
class OSystem_Emscripten : public OSystem_POSIX {
	friend void ::clipboard_paste_callback(char *paste_data);
#ifdef USE_CLOUD
	friend void ::cloud_connection_json_callback(char *str);
#endif
protected:
	Common::String _clipboardCache = "";
#ifdef USE_CLOUD
	CloudConnectionCallback _cloudConnectionCallback;
#endif

public:
	bool hasFeature(Feature f) override;
	void setFeatureState(Feature f, bool enable) override;
	bool getFeatureState(Feature f) override;
	bool displayLogFile() override;
	Common::Path getScreenshotsPath() override;
	Common::Path getDefaultIconsPath() override;
#ifdef USE_OPENGL
	GraphicsManagerType getDefaultGraphicsManager() const override;
#endif
	void exportFile(const Common::Path &filename);
	void init() override;
	void addSysArchivesToSearchSet(Common::SearchSet &s, int priority = 0) override;

#ifdef USE_CLOUD
	void setCloudConnectionCallback(CloudConnectionCallback cb) { _cloudConnectionCallback = cb; }
	bool openUrl(const Common::String &url) override;
#endif // USE_CLOUD

	bool hasTextInClipboard() override;
	Common::U32String getTextFromClipboard() override;
	bool setTextInClipboard(const Common::U32String &text) override;
    
protected:
	Common::Path getDefaultConfigFileName() override;
	Common::Path getDefaultLogFileName() override;
};

#endif
