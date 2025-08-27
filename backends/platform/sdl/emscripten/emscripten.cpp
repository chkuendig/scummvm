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

#ifdef __EMSCRIPTEN__

#define FORBIDDEN_SYMBOL_EXCEPTION_FILE
#define FORBIDDEN_SYMBOL_EXCEPTION_getenv
#include <emscripten.h>

#include "backends/events/emscriptensdl/emscriptensdl-events.h"
#include "backends/fs/emscripten/emscripten-fs-factory.h"
#include "backends/mixer/emscriptensdl/emscriptensdl-mixer.h"
#include "backends/mutex/null/null-mutex.h"
#include "backends/platform/sdl/emscripten/emscripten.h"
#include "backends/timer/emscripten/emscripten-timer.h"
#include "common/file.h"
#ifdef USE_TTS
#include "backends/text-to-speech/emscripten/emscripten-text-to-speech.h"
#endif

extern "C" {
#ifdef USE_CLOUD
void EMSCRIPTEN_KEEPALIVE OSystem_Emscripten_cloudConnectionWizardCallback(char *str) {
	debug(5, "OSystem_Emscripten_cloudConnectionWizardCallback: %s", str);
	OSystem_Emscripten *emscripten_g_system = dynamic_cast<OSystem_Emscripten *>(g_system);
	if (emscripten_g_system->_cloudConnectionCallback) {
		(*emscripten_g_system->_cloudConnectionCallback)(new Common::String(str));
	} else {
		warning("OSystem_Emscripten_cloudConnectionWizardCallback: No Storage Connection Callback Registered!");
	}
}
#endif
}

// Overridden functions

void OSystem_Emscripten::initBackend() {
#ifdef USE_TTS
	// Initialize Text to Speech manager
	_textToSpeechManager = new EmscriptenTextToSpeechManager();
#endif

	// Event source
	_eventSource = new EmscriptenSdlEventSource();

	// Emscripten-specific mixer manager
	_mixerManager = new EmscriptenSdlMixerManager();
	_mixerManager->init();

	// Invoke parent implementation of this method
	OSystem_POSIX::initBackend();
}

void OSystem_Emscripten::init() {

	// SDL Timers don't work in Emscripten unless threads are enabled or Asyncify is disabled.
	// We can do neither, so we use the EmscriptenTimerManager instead.
	// This has to be done before the filesystem is initialized so it's available for folders
	// being loaded over HTTP.
	_timerManager = new EmscriptenTimerManager();

	// Initialze File System Factory
	EmscriptenFilesystemFactory *fsFactory = new EmscriptenFilesystemFactory();
	_fsFactory = fsFactory;

	// Invoke parent implementation of this method
	OSystem_SDL::init();
}

bool OSystem_Emscripten::hasFeature(Feature f) {
	if (f == kFeatureFullscreenMode)
		return true;
	if (f == kFeatureNoQuit)
		return true;
	return OSystem_POSIX::hasFeature(f);
}

bool OSystem_Emscripten::getFeatureState(Feature f) {
	if (f == kFeatureFullscreenMode) {
		return OSystem_Emscripten_isFullscreen();
	} else {
		return OSystem_POSIX::getFeatureState(f);
	}
}

void OSystem_Emscripten::setFeatureState(Feature f, bool enable) {
	if (f == kFeatureFullscreenMode) {
		OSystem_Emscripten_toggleFullscreen(enable);
	} else {
		OSystem_POSIX::setFeatureState(f, enable);
	}
}

Common::Path OSystem_Emscripten::getDefaultLogFileName() {
	return Common::Path("/tmp/scummvm.log");
}

Common::Path OSystem_Emscripten::getDefaultConfigFileName() {
	return Common::Path(Common::String::format("%s/scummvm.ini", getenv("HOME")));
}

Common::Path OSystem_Emscripten::getScreenshotsPath() {
	return Common::Path("/tmp/");
}

Common::Path OSystem_Emscripten::getDefaultIconsPath() {
	return Common::Path(DATA_PATH"/gui-icons/");
}

bool OSystem_Emscripten::displayLogFile() {
	if (_logFilePath.empty())
		return false;

	exportFile(_logFilePath);
	return true;
}

#ifdef USE_OPENGL
OSystem_SDL::GraphicsManagerType OSystem_Emscripten::getDefaultGraphicsManager() const {
	return GraphicsManagerOpenGL;
}
#endif

void OSystem_Emscripten::exportFile(const Common::Path &filename) {
	Common::File file;
	Common::FSNode node(filename);
	file.open(node);
	if (!file.isOpen()) {
		warning("Could not open file %s!", filename.toString(Common::Path::kNativeSeparator).c_str());
		return;
	}
	Common::String exportName = filename.getLastComponent().toString(Common::Path::kNativeSeparator);
	const int32 size = file.size();
	char *bytes = new char[size + 1];
	file.read(bytes, size);
	file.close();
	OSystem_Emscripten_downloadFile(exportName.c_str(), bytes, size);
	delete[] bytes;
}

Common::MutexInternal *OSystem_Emscripten::createMutex() {
	return new NullMutexInternal();
}

void OSystem_Emscripten::addSysArchivesToSearchSet(Common::SearchSet &s, int priority) {
	// Add the global DATA_PATH (and some sub-folders) to the directory search list 
	// Note: gui-icons folder is added in GuiManager::initIconsSet 
	Common::FSNode dataNode(DATA_PATH);
	if (dataNode.exists() && dataNode.isDirectory()) {
		s.addDirectory(dataNode, priority, 2, false);
	}
}

void OSystem_Emscripten::delayMillis(uint msecs) {
	static uint32 lastSleep = 0;
	if (msecs == 0 && getMillis() - lastSleep < 20) {
		return;
	}
#ifdef ENABLE_EVENTRECORDER
	if (!g_eventRec.processDelayMillis())
#endif
	SDL_Delay(msecs);

	((EmscriptenTimerManager *)_timerManager)->checkTimers();
	lastSleep = getMillis();
}

#ifdef USE_CLOUD
bool OSystem_Emscripten::openUrl(const Common::String &url) {
	if (url.hasPrefix("https://cloud.scummvm.org")) {
		return OSystem_Emscripten_openCloudOAuthWindow(url.c_str());
	}
	return OSystem_SDL::openUrl(url);
}
#endif

#endif
