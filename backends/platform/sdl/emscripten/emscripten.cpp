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
#include "backends/fs/emscripten/dragdrop-fs.h"
#include "backends/fs/emscripten/emscripten-fs-factory.h"
#include "backends/mixer/emscriptensdl/emscriptensdl-mixer.h"
#include "backends/mutex/null/null-mutex.h"
#include "backends/fs/emscripten/emscripten-fs-factory.h"
#include "backends/platform/sdl/emscripten/emscripten.h"
#include "backends/timer/emscripten/emscripten-timer.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/translation.h"
#ifdef USE_TTS
#include "backends/text-to-speech/emscripten/emscripten-text-to-speech.h"
#endif

// Inline JavaScript, see https://emscripten.org/docs/api_reference/emscripten.h.html#inline-assembly-javascript for details
EM_JS(bool, isFullscreen, (), {
	return !!document.fullscreenElement;
});

EM_JS(void, toggleFullscreen, (bool enable), {
	let canvas = document.getElementById('canvas');
	if (enable && !document.fullscreenElement) {
		canvas.requestFullscreen();
	}
	if (!enable && document.fullscreenElement) {
		document.exitFullscreen();
	}
});

EM_JS(void, downloadFile, (const char *filenamePtr, char *dataPtr, int dataSize), {
	const view = new Uint8Array(HEAPU8.buffer, dataPtr, dataSize);
	const blob = new Blob([view], {
			type:
				'octet/stream'
		});
	const filename = UTF8ToString(filenamePtr);
	setTimeout(() => {
		const a = document.createElement('a');
		a.style = 'display:none';
		document.body.appendChild(a);
		const url = window.URL.createObjectURL(blob);
		a.href = url;
		a.download = filename;
		a.click();
		window.URL.revokeObjectURL(url);
		document.body.removeChild(a);
	}, 0);
});

#ifdef USE_CLOUD
/* Listener to feed the activation JSON from the wizard at cloud.scummvm.org back 
 * Usage: Run the following on the final page of the activation flow:
 * 		  window.opener.postMessage(document.getElementById("json").value,"*")
 */
EM_JS(bool, cloud_connection_open_oauth_window, (char const *url), {
	oauth_window = window.open(UTF8ToString(url));
	window.addEventListener("message", (event) => {
		Module._cloud_connection_json_callback(stringToNewUTF8( JSON.stringify(event.data)));
		oauth_window.close()
	}, {once : true});
	return true;
});
#endif

extern "C" {
#ifdef USE_CLOUD
void EMSCRIPTEN_KEEPALIVE cloud_connection_json_callback(char *str) {
	warning("cloud_connection_callback: %s", str);
	OSystem_Emscripten *emscripten_g_system = dynamic_cast<OSystem_Emscripten *>(g_system);
	if (emscripten_g_system->_cloudConnectionCallback) {
		(*emscripten_g_system->_cloudConnectionCallback)(new Common::String(str));
	} else {
		warning("No Storage Connection Callback Registered!");
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

	ConfMan.setPath("extrapath", Common::Path(Common::String::format("%s/extras/", getenv("HOME"))));

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
		return isFullscreen();
	} else {
		return OSystem_POSIX::getFeatureState(f);
	}
}

void OSystem_Emscripten::setFeatureState(Feature f, bool enable) {
	if (f == kFeatureFullscreenMode) {
		toggleFullscreen(enable);
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
	downloadFile(exportName.c_str(), bytes, size);
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

bool OSystem_Emscripten::setGraphicsMode(int mode, uint flags) {
	debug(3, "Removing drag & drop event listeners before setGraphicsMode");
	DragDropFilesystemNode_removeDragEventListeners();
	bool ok = OSystem_SDL::setGraphicsMode(mode, flags);
	if (ok) {
		debug(3, "Re-adding drag & drop event listeners after setGraphicsMode");
		DragDropFilesystemNode_addDragEventListeners();
	}
	return ok;
}

void OSystem_Emscripten::applyBackendSettings() {
	// Remove SDL3 drag-and-drop listeners and add our own.
	// SDL3 default listeners don't support directories, are buggy (libsdl-org/SDL#13924) and
	// load every file into memory after dropping
	DragDropFilesystemNode_removeDragEventListeners();
	DragDropFilesystemNode_addDragEventListeners();
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

void OSystem_Emscripten::importExtrasFile(const Common::FSNode &node) {
	assert(!node.isDirectory());
	assert(ConfMan.hasKey("extrapath") || ConfMan.hasDefault("extrapath"));
	Common::Path extrapath = ConfMan.getPath("extrapath");
	Common::Path readPath = node.getPath();
	Common::String filename = readPath.getLastComponent().toString();

	// Import Roland MT-32 and CM-32L ROM files
	if (filename == "MT32_PCM.ROM" || filename == "MT32_CONTROL.ROM" ||
		filename == "CM32L_PCM.ROM" || filename == "CM32L_CONTROL.ROM") {

		Common::File readFile;
		if (!readFile.open(node)) {
			warning("OSystem_Emscripten::importExtrasFile - Could not open file %s", readPath.toString().c_str());
			readFile.close();
			return;
		}
		const Common::Path writePath(extrapath.appendComponent(filename.c_str()));
		Common::DumpFile writeFile;
		if (!writeFile.open(writePath)) {
			writeFile.close();
			readFile.close();
			warning("OSystem_Emscripten::importExtrasFile - Could not open file %s", writePath.toString().c_str());
			return;
		}
		byte *_buffer = new byte[readFile.size()];
		uint32 readBytes = readFile.read(_buffer, readFile.size());
		if (readBytes == readFile.size()) {
			if (writeFile.write(_buffer, readBytes) != readBytes) {
				warning("OSystem_Emscripten::importExtrasFile - unable to write all received bytes into output file");
				writeFile.close();
				readFile.close();
				return;
			}
		} else {
			writeFile.close();
			readFile.close();
			warning("OSystem_Emscripten::importExtrasFile - unable to read all bytes from input file");
			return;
		}
		writeFile.close();
		readFile.close();
		debug(5, "OSystem_Emscripten::importExtrasFile - File copied %s -> %s", filename.c_str(), writePath.toString().c_str());

		Common::FSNode mt32PcmNode = Common::FSNode(extrapath.appendComponent("MT32_PCM.ROM"));
		Common::FSNode mt32ControlNode = Common::FSNode(extrapath.appendComponent("MT32_CONTROL.ROM"));
		Common::FSNode cm32lPcmNode = Common::FSNode(extrapath.appendComponent("CM32L_PCM.ROM"));
		Common::FSNode cm32lControlNode = Common::FSNode(extrapath.appendComponent("CM32L_CONTROL.ROM"));
		const bool mt32Complete = mt32PcmNode.exists() && mt32ControlNode.exists();
		const bool cm32Complete = cm32lPcmNode.exists() && cm32lControlNode.exists();
		if ((filename.equals("MT32_PCM.ROM") || filename.equals("MT32_CONTROL.ROM")) && mt32Complete) {
			g_system->displayMessageOnOSD(_("Roland MT-32 ROMs imported successfully"));
			debug(5, "OSystem_Emscripten::importExtrasFile - Roland MT-32 ROMs imported successfully");
		} else if ((filename.equals("CM32L_PCM.ROM") || filename.equals("CM32L_CONTROL.ROM")) && cm32Complete) {
			g_system->displayMessageOnOSD(_("Roland CM-32L ROMs imported successfully"));
			debug(5, "OSystem_Emscripten::importExtrasFile - Roland CM-32L ROMs imported successfully");
		}
	}
}

#ifdef USE_CLOUD
bool OSystem_Emscripten::openUrl(const Common::String &url) {
	if(url.hasPrefix("https://cloud.scummvm.org/")){
		return cloud_connection_open_oauth_window(url.c_str());
	}
	return	OSystem_SDL::openUrl(url);
}
#endif

#endif
