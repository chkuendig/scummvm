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

#include "common/config-manager.h"
#include "common/memstream.h"
#include "common/printman.h"
#include "common/rect.h"
#include "common/str.h"
#include "common/str-array.h"
#include "graphics/managed_surface.h"

#ifdef USE_PNG
#include "image/png.h"
#else
#include "image/bmp.h"
#endif

#include "emscripten-printman.h"

// Implemented in the Emscripten JS library; declared with C linkage in
// backends/platform/sdl/emscripten/emscripten.h. Wraps the data in a Blob and
// triggers a browser download.
extern "C" void OSystem_Emscripten_downloadFile(const char *filenamePtr, char *dataPtr, int dataSize);

namespace {

// The web has no real printer, so the print dialog offers a single virtual
// "Download Image" target whose doPrint() streams the page image straight to a
// browser download. Advertising it as a printer also stops the dialog from
// auto-selecting "Save as image", which would otherwise write an unreachable
// copy into the IDBFS save area.
class EmscriptenPrintingManager : public Common::PrintingManager {
protected:
	Common::StringArray listPrinterNames() const override {
		Common::StringArray names;
		names.push_back("Download Image");
		return names;
	}

	Common::String getDefaultPrinterName() const override {
		return "Download Image";
	}

	void doPrint(const Graphics::ManagedSurface &surf, const Common::Rect &destRect) override;
};

void EmscriptenPrintingManager::doPrint(const Graphics::ManagedSurface &surf, const Common::Rect &destRect) {
#ifdef USE_PNG
	const char *ext = "png";
#else
	const char *ext = "bmp";
#endif

	Common::String name = Common::String::format("%s-printout.%s", ConfMan.getActiveDomainName().c_str(), ext);

	Common::MemoryWriteStreamDynamic stream(DisposeAfterUse::YES);

	byte palette[256 * 3];
	surf.grabPalette(palette, 0, 256);

#ifdef USE_PNG
	Image::writePNG(stream, surf, palette);
#else
	Image::writeBMP(stream, surf, palette);
#endif

	OSystem_Emscripten_downloadFile(name.c_str(), (char *)stream.getData(), (int)stream.size());
}

} // End of anonymous namespace

Common::PrintingManager *createEmscriptenPrintingManager() {
	return new EmscriptenPrintingManager();
}

#endif
