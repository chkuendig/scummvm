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

#include "backends/fs/emscripten/emscripten-fs-factory.h"
#include "backends/platform/sdl/emscripten/emscripten.h"
#include "common/file.h"
#include "common/translation.h"

// Inline JavaScript, 
// see https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html




EM_JS(void, downloadFile, (const char *filenamePtr, char *dataPtr, int dataSize), {
	const view = new Uint8Array(Module.HEAPU8.buffer, dataPtr, dataSize);
    // Copy the view to a new array (Blob constructor cant handle SharedArrayBuffers)
    const copy = new Uint8ClampedArray( [view] );
	const blob = new Blob([copy], {
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

/*
 * Experimental Clipboard Support.
 * Works in Chrome but not in Safari and Firefox (Mac):
 * - Firefox (Mac) doesn't recognize meta/cmd key - https://stackoverflow.com/a/71656492
 * - Safari requires workarounds for paste ( https://stackoverflow.com/a/17105405 ) and copy seems to be triggered at the wrong time (causing a NotAllowedError)
 */
EM_JS(void, clipboard_copy, (char const *content_ptr), {
	navigator.clipboard.writeText(UTF8ToString(content_ptr));
});

EM_JS(void, clipboard_add_paste_listener, (), {
	//todo: FIx document is not defined when running multithreaded 
	/*document.addEventListener(
		'paste', (event) => {
			Module._clipboard_paste_callback(stringToNewUTF8(event.clipboardData.getData('text/plain')));
		});*/
});

#ifdef USE_CLOUD
/* Listener to feed the activation JSON from the wizard at cloud.scummvm.org back 
 * Get the OAuth response token from the php session. ScummVM Webassembly is polling the authentication service  
 * regularly to check if the authorization has finished. If done, the token is immediately cleared. (by setting clear=true)
 * 
 * Usually in a typical OAuth scenario this isn't needed as the authentication page could communicate with initiating page via sendMessage to transmit the token, 
 * but since we are using pthreads, the initiating page has to run with cross origin isolation to access SharedArrayBuffer. Which means that the two pages
 * can't connect (even if they were on the same domain as the authentication redirects to the authentication provider, breaking the "same-origin"). 
 * This is a well known issues, see e.g. the notice at https://web.dev/articles/coop-coep#integrate_coop_and_coep. By using this polling mechanism, we can 
 * work around this issue to get the token as long as the CORP (Cross-Origin-Resource-Policy) is set to cross-origin.
 */
EM_JS(bool, cloud_connection_open_oauth_window, (char const *url), {
	oauth_window = window.open(UTF8ToString(url));
	const intervalID = setInterval(async () => {
	 	try {
			// TODO: this should give up after a certain time (e.g. 2mins) to avoid infinitely polling the cloud service (user could still manually enter the token)
            const response = await fetch("https://cloud.scummvm.org/response_token/?clear=true",{ credentials: 'include' });
            if (!response.ok) {
              throw new Error(`Response status: ${response.status}`);
            }
            const json = await response.json();
            if(Object.keys(json).length > 0){
                clearInterval(intervalID);
				Module._cloud_connection_json_callback(stringToNewUTF8( JSON.stringify(json)));
            }
            console.log(json);
          } catch (error) {
            console.error(error.message);
          }
	}, 500);
	return true;
});
#endif

extern "C" {
void EMSCRIPTEN_KEEPALIVE clipboard_paste_callback(char *paste_data) {
	OSystem_Emscripten *emscripten_g_system = dynamic_cast<OSystem_Emscripten *>(g_system);
	emscripten_g_system->_clipboardCache = Common::String(paste_data);
}

EMSCRIPTEN_KEEPALIVE void  store_file(char const *filename_c_str, char const *mime_type, char *buffer, size_t buffer_size){
	// Store a file - this function is called from javascript when a file is sent via drag & drop.
	// The file was not uploaded.
  	if (! buffer || buffer_size == 0) {
 	   	warning("File not uploaded %s",filename_c_str);
		return;
  	}
  
  	/// Ok
  	warning("File uploaded %s",filename_c_str);
	const Common::Path &path =  Common::Path(Common::String::format("%s/%s", getenv("HOME"),filename_c_str));
	Common::DumpFile out;
	if (!out.open(path)) {
		warning("Could not open file %s!", path.toString().c_str());
		return;
	}
	out.write(buffer, buffer_size);
	Common::String filename = Common::String(filename_c_str);
	if ((filename.equals("MT32_PCM.ROM") || filename.equals("MT32_CONTROL.ROM")) && 
			Common::File::exists(Common::Path("MT32_PCM.ROM")) &&
			Common::File::exists(Common::Path("MT32_CONTROL.ROM"))) {
		g_system->displayMessageOnOSD(_("Roland MT-32 ROMs uploaded successfully"));
		warning("Roland MT-32 ROMs uploaded successfully");
	} else if ((filename.equals("CM32L_PCM.ROM") || filename.equals("CM32L_CONTROL.ROM")) && 
			Common::File::exists(Common::Path("CM32L_PCM.ROM")) &&
			Common::File::exists(Common::Path("CM32L_CONTROL.ROM"))) {
		g_system->displayMessageOnOSD(_("Roland CM-32L ROMs uploaded successfully"));
		warning("Roland CM-32L ROMs uploaded successfully");
	} else {
		warning("File %s saved to successfully to %s", filename_c_str, getenv("HOME"));
	}

}

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
void OSystem_Emscripten::init() {
	// Initialze File System Factory
	EmscriptenFilesystemFactory *fsFactory = new EmscriptenFilesystemFactory();
	_fsFactory = fsFactory;

	// setup JS clipboard listener
	clipboard_add_paste_listener();

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
		const bool result = MAIN_THREAD_EM_ASM_INT({
			return !!document.fullscreenElement;
		});
		return result;
	} else {
		return OSystem_POSIX::getFeatureState(f);
	}
}

void OSystem_Emscripten::setFeatureState(Feature f, bool enable) {
	if (f == kFeatureFullscreenMode) {
		MAIN_THREAD_EM_ASM({
			var enable = !!$0;
			let canvas = document.getElementById('canvas');
			if (enable && !document.fullscreenElement) {
				canvas.requestFullscreen();
			}
			if (!enable && document.fullscreenElement) {
				document.exitFullscreen();
			}
		}, enable);
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

void OSystem_Emscripten::addSysArchivesToSearchSet(Common::SearchSet &s, int priority) {
	// Invoke parent implementation of this method
	OSystem_POSIX::addSysArchivesToSearchSet(s, priority);

	// Add home folder 
	Common::Path homePath = Common::Path(getenv("HOME"));
	if (!homePath.empty()) {
		// Success: search with a depth of 2 so the shaders are found
		s.add("HOME", new Common::FSDirectory(homePath, 2), priority);
	}
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

bool OSystem_Emscripten::hasTextInClipboard() {
	return _clipboardCache.size() > 0;
}

Common::U32String OSystem_Emscripten::getTextFromClipboard() {
	if (!hasTextInClipboard())
		return Common::U32String();
	return _clipboardCache.decode();
}

bool OSystem_Emscripten::setTextInClipboard(const Common::U32String &text) {
	// The encoding we need to use is UTF-8.
	_clipboardCache = text.encode();
	clipboard_copy(_clipboardCache.c_str());
	return 0;
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
