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
#include "backends/mutex/null/null-mutex.h"
#include "backends/platform/sdl/emscripten/emscripten.h"
#include "backends/timer/default/default-timer.h"
#include "common/file.h"
#include "common/std/algorithm.h"
#include "common/util.h"
#ifdef USE_OPENGL
#include "backends/graphics/opengl/opengl-graphics.h"
#endif
#include "graphics/font.h"
#include "graphics/fontman.h"
#ifdef USE_TTS
#include "backends/text-to-speech/emscripten/emscripten-text-to-speech.h"
#endif
#ifdef USE_CLOUD
#include "backends/cloud/cloudmanager.h"
#endif

extern "C" {
#ifdef USE_CLOUD
void EMSCRIPTEN_KEEPALIVE cloudConnectionWizardCallback(char *str) {
	debug(5, "cloudConnectionWizardCallback: %s", str);
	OSystem_Emscripten *emscripten_g_system = dynamic_cast<OSystem_Emscripten *>(g_system);
	if (emscripten_g_system->_cloudConnectionCallback) {
		(*emscripten_g_system->_cloudConnectionCallback)(new Common::String(str));
	} else {
		warning("cloudConnectionWizardCallback: No Storage Connection Callback Registered!");
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

	// Invoke parent implementation of this method
	OSystem_POSIX::initBackend();
}

void OSystem_Emscripten::init() {

	// SDL Timers don't work in Emscripten unless threads are enabled or Asyncify is disabled.
	// We can do neither, so we use the DefaultTimerManager instead.
	// This has to be done before the filesystem is initialized so it's available for folders
	// being loaded over HTTP. 
	_timerManager = new DefaultTimerManager();

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

void OSystem_Emscripten::updateTimers() {
	// avoid a recursion loop if a timer callback decides to call OSystem::delayMillis()
	static bool inTimer = false;

	if (!inTimer) {
		inTimer = true;
		((DefaultTimerManager *)_timerManager)->checkTimers();
		inTimer = false;
	} else {
		const Common::ConfigManager::Domain *activeDomain = ConfMan.getActiveDomain();
		assert(activeDomain);

		warning("%s/%s calls update() from timer",
				activeDomain->getValOrDefault("engineid").c_str(),
				activeDomain->getValOrDefault("gameid").c_str());
	}
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
	static uint32 lastThreshold = 0;
	const uint32 threshold = getMillis() + msecs;
	if (msecs == 0 && threshold - lastThreshold < 10) {
		return;
	}
	uint32 pause = 0;
	do {
#ifdef ENABLE_EVENTRECORDER
		if (!g_eventRec.processDelayMillis())
#endif
		SDL_Delay(pause);
		updateTimers();
		pause = getMillis() > threshold ? 0 : threshold - getMillis(); // Avoid negative values
		pause = pause > 10 ? 10 : pause; // ensure we don't pause for too long
	} while (pause > 0);
	lastThreshold = threshold;
	if (getCloudIcon() && getCloudIcon()->needsUpdate()) {
		getCloudIcon()->update();
	}
}

Cloud::CloudIcon *OSystem_Emscripten::getCloudIcon() {
#ifndef USE_CLOUD
	if (!_cloudIcon) {
		_cloudIcon = new Cloud::CloudIcon();
	}
	return _cloudIcon;
#else
	return CloudMan.getCloudIcon();
#endif
}
#ifdef USE_CLOUD
bool OSystem_Emscripten::openUrl(const Common::String &url) {
	if(url.hasPrefix("https://cloud.scummvm.org/")){
		return cloudConnectionWizardOAuthWindow(url.c_str());
	}
	return	OSystem_SDL::openUrl(url);
}
#endif

void OSystem_Emscripten::updateDownloadProgress(int currentBytes, int totalBytes, int downloadStartTime) {
	int elapsedMs = g_system->getMillis() - downloadStartTime;
	if (totalBytes <= 0 || elapsedMs <= 0) {
		return;
	}
	// Calculate progress from bytes

	if (_graphicsManager) {

		double progress = (double)currentBytes / totalBytes;

		// Make progress bar appropriately sized but within overlay bounds
		int wRect = _graphicsManager->getOverlayWidth() * 0.7;
		int lRect = 80;
		int borderWidth = 3;
		// Get overlay pixel format for proper color handling
		Graphics::PixelFormat overlayFormat = _graphicsManager->getOverlayFormat();

		// Create a temporary surface for drawing
		Graphics::Surface tempSurface;
		tempSurface.create(wRect, lRect, overlayFormat);

		int wProgressFill = (int)((wRect - 2 * borderWidth) * progress);
		uint32 bgColor = overlayFormat.ARGBToColor(128, 60, 60, 60); // Semi-transparent dark gray
		uint32 borderColor = overlayFormat.RGBToColor(0, 0, 0);      // Black border
		uint32 fillColor = overlayFormat.RGBToColor(255, 255, 255);  // White fill

		// Fill background
		tempSurface.fillRect(Common::Rect(0, 0, wRect, lRect), bgColor);
		// Draw border
		for (int b = 0; b < borderWidth; b++) {
			tempSurface.frameRect(Common::Rect(b, b, wRect - b, lRect - b), borderColor);
		}
		// Draw progress fill
		if (wProgressFill > 0) {
			tempSurface.fillRect(Common::Rect(borderWidth, borderWidth, borderWidth + wProgressFill, lRect - borderWidth), fillColor);
		}

		// Draw text elements
		const Graphics::Font *localizedFont = FontMan.getFontByUsage(Graphics::FontManager::kLocalizedFont);

		// Percentage text (centered)
		Common::String percentText = Common::String::format("%.0f%%", progress * 100);
		int percentWidth = localizedFont->getStringWidth(percentText);
		int percentHeight = localizedFont->getFontHeight();
		int percentX = (wRect - percentWidth) / 2;
		int percentY = (lRect - percentHeight) / 2;
		uint32 percentColor = (percentX < borderWidth + wProgressFill) && (percentX + percentWidth > borderWidth) ? overlayFormat.RGBToColor(0, 0, 0) : // Black text on white fill
								  overlayFormat.RGBToColor(255, 255, 255);                                                                              // White text on dark background
		localizedFont->drawString(&tempSurface, percentText, percentX, percentY, percentWidth, percentColor, Graphics::kTextAlignCenter);

		// Progress bytes text (bottom-left aligned)
		const char *currentUnits, *totalUnits;
		Common::String currentBytesStr = Common::getHumanReadableBytes((uint64)currentBytes, currentUnits);
		Common::String totalBytesStr = Common::getHumanReadableBytes((uint64)totalBytes, totalUnits);
		Common::String progressText = Common::String::format("%s%s/%s%s", currentBytesStr.c_str(), currentUnits, totalBytesStr.c_str(), totalUnits);
		int progressWidth = localizedFont->getStringWidth(progressText);
		int progressX = borderWidth + 3;                                                                                                                    // Left aligned with small margin
		int progressY = lRect - localizedFont->getFontHeight() - borderWidth - 1;                                                                           // Bottom aligned with small margin
		uint32 progressColor = (progressX < borderWidth + wProgressFill) && (progressX + progressWidth > borderWidth) ? overlayFormat.RGBToColor(0, 0, 0) : // Black text on white fill
								   overlayFormat.RGBToColor(255, 255, 255);                                                                                 // White text on dark background
		localizedFont->drawString(&tempSurface, progressText, progressX, progressY, progressWidth, progressColor, Graphics::kTextAlignLeft);

		// Speed text (bottom-right aligned)
		if (currentBytes > 0) {
			double bytesPerSecond = (double)currentBytes / (elapsedMs / 1000.0);
			const char *speedUnits;
			Common::String speed = Common::getHumanReadableBytes((uint64)bytesPerSecond, speedUnits);
			Common::String speedText = Common::String::format("%s %s/s", speed.c_str(), speedUnits);
			int speedWidth = localizedFont->getStringWidth(speedText);
			int speedX = wRect - speedWidth - borderWidth - 2;                                                                                      // Right aligned with small margin
			int speedY = lRect - localizedFont->getFontHeight() - borderWidth - 3;                                                                  // Bottom aligned with small margin
			uint32 speedColor = (speedX < borderWidth + wProgressFill) && (speedX + speedWidth > borderWidth) ? overlayFormat.RGBToColor(0, 0, 0) : // Black text on white fill
									overlayFormat.RGBToColor(255, 255, 255);                                                                        // White text on dark background
			localizedFont->drawString(&tempSurface, speedText, speedX, speedY, speedWidth, speedColor, Graphics::kTextAlignLeft);
		}

		// Copy to overlay
		int x = Std::max(0, (_graphicsManager->getOverlayWidth() - wRect) / 2); // Center horizontally
		int y = Std::max(10, _graphicsManager->getOverlayHeight() - 2 * lRect); // Bottom with spacing equal to height (40px from bottom)
		_graphicsManager->copyRectToOverlay(tempSurface.getPixels(), tempSurface.pitch, x, y, wRect, lRect);
		_graphicsManager->updateScreen();

		// Clean up
		tempSurface.free();
	}
}

#endif
