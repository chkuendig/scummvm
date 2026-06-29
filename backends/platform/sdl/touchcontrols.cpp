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

#include "backends/platform/sdl/touchcontrols.h"
#include "backends/platform/sdl/sdl.h"

#include "common/config-manager.h"
#include "common/file.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/util.h"
#include "graphics/fontman.h"
#include "graphics/font.h"
#include "graphics/managed_surface.h"
#include "graphics/svg.h"

#include <math.h>

// ---------------------------------------------------------------------------
// gamepad.svg sprite-sheet layout (in SVG pixels, 384x256, three 128x128
// columns each with a "resting" row and a "pressed" highlight row):
//   col 0 row 0 (0,0)     : D-pad base (circle + grey arrows)
//   col 0 row 1 (0,128)   : D-pad pressed arrows (white highlights)
//   col 1 row 0 (128,0)   : A/B/X/Y resting cluster
//   col 1 row 1 (128,128) : A/B/X/Y pressed highlights
//   col 2 row 0 (256,0)   : labelled circles (the menu "lens" reused for Start)
//   col 2 row 1 (256,128) : (system highlights, unused here)
//
// A spare row is appended below the 384x256 sheet (SVG y 256..384) into which
// the ScummVM "S" button and the L1/R1 pills are drawn from primitives at load
// time (see renderPrimitives()).
// ---------------------------------------------------------------------------
#define SVG_WIDTH          384
#define SVG_HEIGHT         256
// Total height of the backing surface: the sheet plus the spare primitives row.
#define SVG_FULL_HEIGHT    384
#define BG_WIDTH           128
#define BG_HEIGHT          128
#define BUTTON_WIDTH        64
#define BUTTON_HEIGHT       64

// A/B/X/Y resting glyph crops from the cluster (col 1 row 0), one per button.
#define ABXY_REST_Y         160,   0, 224,  64
#define ABXY_REST_B         192,  32, 256,  96
#define ABXY_REST_A         160,  64, 224, 128
#define ABXY_REST_X         128,  32, 192,  96
// A/B/X/Y pressed-highlight glyphs (col 1 row 1).
#define ABXY_HL_Y           128, 128, 192, 192
#define ABXY_HL_B           192, 128, 256, 192
#define ABXY_HL_A           128, 192, 192, 256
#define ABXY_HL_X           192, 192, 256, 256

// Spacing of the diamond buttons from the cluster centre (SVG px) and the hit
// radius around each (SVG px). Spread wider than the natural 32px glyph spacing
// so the four hotspots do not overlap (independent multi-touch presses).
#define ABXY_SPACING        44
#define ABXY_HIT            30

// Start/menu glyph reuses the lower labelled circle of column 2 (the silver
// menu "lens"), a 48x48 region centred on the glyph at SVG (320, 100).
#define START_CLIP          296, 76, 344, 124
#define START_HIT           30

// Floating D-pad geometry (SVG px). The base cell fills a 128 cell; the pressed
// arrow highlights are 64x64 crops of the highlight row (col 0 row 1).
#define DPAD_BASE_CLIP        0,   0, 128, 128
#define DPAD_ARROW_W        64
#define DPAD_ARROW_H        64
#define DPAD_HL_TOP        128
#define DPAD_DEAD           21   // direction dead-zone (SVG px)
#define DPAD_HIT            62   // hit radius for zombie re-touch / inside test

// ScummVM "S" button, drawn from primitives into the spare row. Opens the GMM.
#define SCUMM_CLIP            0, 256,  48, 304
#define SCUMM_HIT           26

// L1 / R1 shoulder pills, drawn into the spare row.
#define L1_CLIP             224, 258, 320, 302
#define R1_CLIP             224, 306, 320, 350
#define SHOULDER_HIT        48

#define ALPHA_OPAQUE       255
#define ZOMBIE_TIMEOUT     500 // ms

// Drag-to-look: a finger that lifts having travelled less than this (window px)
// synthesizes a left click instead.
#define TAP_MAX_TRAVEL       8

// ---------------------------------------------------------------------------
// Home (resting) positions of the controls, in percent of the window. AAA
// touch-FPS layout in landscape: D-pad bottom-left, A/B/X/Y diamond
// bottom-right, shoulders top corners, Start top-centre, the ScummVM button in
// the top-left corner and an invisible drag-to-look region filling the rest of
// the right half.
// ---------------------------------------------------------------------------
#define DIR_HOME_X_PCT       18
#define DIR_HOME_Y_PCT       75
#define ABXY_HOME_X_PCT      85
#define ABXY_HOME_Y_PCT      75
#define SHOULDER_L_X_PCT      9
#define SHOULDER_R_X_PCT     91
#define SHOULDER_Y_PCT       12
#define START_HOME_X_PCT     50
#define START_HOME_Y_PCT      8
// ScummVM/GMM button: top-left corner, clear of the L1 pill and the mode-toggle
// icon (which lives in the top-right corner).
#define SCUMM_HOME_X_PCT      4
#define SCUMM_HOME_Y_PCT      7

// Fingers left of this (percent of width) and not on a hotspot drive the
// D-pad; everything else drives drag-to-look.
#define LEFT_REGION_MAX_PCT  40

// The scale factor is stored as a fixed point 30.2 bits
// This avoids floating point operations
#define SCALE_FACTOR_FXP 4

// gamepad.svg was designed with a basis of 128x128 sized widget
// As it's too small on screen, we apply a factor of 1.5x
// It can be tuned here and in SVG viewBox without changing anything else
#define SVG_UNSCALED(x) ((x) * 3 / 2)
#define SVG_SQ_UNSCALED(x) ((x * x) * 9 / 4)

#define SVG_SCALED(x) (SVG_UNSCALED(x) * _scale)
#define SCALED_PIXELS(x) ((x) / SCALE_FACTOR_FXP)

#define SVG_PIXELS(x) SCALED_PIXELS(SVG_SCALED(x))

#define FUNC_SVG_SQ_SCALED(x) (SVG_SQ_UNSCALED(x) * parent->_scale2)

TouchControls::TouchControls() :
	_drawer(nullptr),
	_screen_width(0),
	_screen_height(0),
	_scale(0),
	_scale2(0),
	_svg(nullptr),
	_loadFailed(false),
	_zombieCount(0),
	_baseAlpha(ALPHA_OPAQUE * 6 / 10) {
	_gesturesInsets[0] = _gesturesInsets[1] = _gesturesInsets[2] = _gesturesInsets[3] = 0;
	_functions[kFunctionDpad]      = new FunctionDpad(this);
	_functions[kFunctionButtonY]   = new FunctionButton(this, Common::JOYSTICK_BUTTON_Y,
			0, -ABXY_SPACING, Common::Rect(ABXY_REST_Y), Common::Rect(ABXY_HL_Y));
	_functions[kFunctionButtonB]   = new FunctionButton(this, Common::JOYSTICK_BUTTON_B,
			ABXY_SPACING, 0, Common::Rect(ABXY_REST_B), Common::Rect(ABXY_HL_B));
	_functions[kFunctionButtonA]   = new FunctionButton(this, Common::JOYSTICK_BUTTON_A,
			0, ABXY_SPACING, Common::Rect(ABXY_REST_A), Common::Rect(ABXY_HL_A));
	_functions[kFunctionButtonX]   = new FunctionButton(this, Common::JOYSTICK_BUTTON_X,
			-ABXY_SPACING, 0, Common::Rect(ABXY_REST_X), Common::Rect(ABXY_HL_X));
	_functions[kFunctionShoulderL] = new FunctionShoulder(this, Common::JOYSTICK_BUTTON_LEFT_SHOULDER, false);
	_functions[kFunctionShoulderR] = new FunctionShoulder(this, Common::JOYSTICK_BUTTON_RIGHT_SHOULDER, true);
	_functions[kFunctionStart]     = new FunctionStart(this);
	_functions[kFunctionScummVM]   = new FunctionScummVM(this);
	_functions[kFunctionLook]      = new FunctionLook(this);
}

void TouchControls::init(float scale) {
	// Don't re-open (and on Emscripten re-fetch) the asset once it has failed:
	// init() is otherwise retried lazily every frame while uninitialized.
	if (_loadFailed) {
		return;
	}

	// Register the shared gamepad opacity setting (same key/default as the iOS
	// backend) so reads below never hit an unregistered key.
	ConfMan.registerDefault("gamepad_controller_opacity", 6);

	_scale = scale * SCALE_FACTOR_FXP;
	// As scale is small, this should fit in int
	_scale2 = _scale * _scale;

	Common::File stream;

	if (!stream.open("gamepad.svg")) {
		// Non-fatal: the gamepad stays unavailable if never bundled. Latch the
		// failure so we stop probing for the file on every frame.
		_loadFailed = true;
		return;
	}

	// Rasterize the sheet, then copy it into a taller surface so we have a spare
	// row below it to draw the thumbstick ring/knob and the L1/R1 pills into.
	Graphics::SVGBitmap sheet(&stream, SVG_PIXELS(SVG_WIDTH), SVG_PIXELS(SVG_HEIGHT));

	delete _svg;
	_svg = new Graphics::ManagedSurface(SVG_PIXELS(SVG_WIDTH), SVG_PIXELS(SVG_FULL_HEIGHT), sheet.format);
	_svg->simpleBlitFrom(sheet);
	renderPrimitives();
}

void TouchControls::renderPrimitives() {
	if (!_svg) {
		return;
	}

	Graphics::PixelFormat fmt = _svg->format;

	// Clear the spare row to fully transparent.
	_svg->fillRect(Common::Rect(0, SVG_PIXELS(SVG_HEIGHT), SVG_PIXELS(SVG_WIDTH), SVG_PIXELS(SVG_FULL_HEIGHT)),
			fmt.ARGBToColor(0, 0, 0, 0));

	const uint32 pillFill = fmt.ARGBToColor(150, 25,  25,  30);
	const uint32 pillEdge = fmt.ARGBToColor(220, 235, 235, 235);
	const uint32 textCol  = fmt.ARGBToColor(255, 245, 245, 245);

	const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	if (!font) {
		font = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	}

	// --- ScummVM "S" button (round badge with an "S" glyph) ---
	{
		Common::Rect c(SCUMM_CLIP);
		Common::Rect r(SVG_PIXELS(c.left), SVG_PIXELS(c.top),
				SVG_PIXELS(c.right), SVG_PIXELS(c.bottom));
		int cx = (r.left + r.right) / 2;
		int cy = (r.top + r.bottom) / 2;
		int rOut = r.width() / 2 - 1;
		_svg->drawEllipse(cx - rOut, cy - rOut, cx + rOut, cy + rOut, pillFill, true);
		_svg->drawEllipse(cx - rOut, cy - rOut, cx + rOut, cy + rOut, pillEdge, false);
		if (font) {
			int ty = cy - font->getFontHeight() / 2;
			font->drawString(_svg, "S", r.left, ty, r.width(), textCol,
					Graphics::kTextAlignCenter);
		}
	}

	// --- L1 / R1 pills with labels ---
	struct { Common::Rect clip; const char *label; } pills[2] = {
		{ Common::Rect(L1_CLIP), "L1" },
		{ Common::Rect(R1_CLIP), "R1" }
	};
	for (int i = 0; i < 2; i++) {
		Common::Rect r(SVG_PIXELS(pills[i].clip.left),  SVG_PIXELS(pills[i].clip.top),
				SVG_PIXELS(pills[i].clip.right), SVG_PIXELS(pills[i].clip.bottom));
		int arc = r.height() / 2;
		_svg->drawRoundRect(r, arc, pillFill, true);
		_svg->drawRoundRect(r, arc, pillEdge, false);
		if (font) {
			int ty = r.top + (r.height() - font->getFontHeight()) / 2;
			font->drawString(_svg, pills[i].label, r.left, ty, r.width(), textCol,
					Graphics::kTextAlignCenter);
		}
	}
}

TouchControls::~TouchControls() {
	delete _svg;
	for(unsigned int i = 0; i < kFunctionCount; i++) {
		delete _functions[i];
	}
}

void TouchControls::refreshConfig() {
	int opacity = CLIP(ConfMan.getInt("gamepad_controller_opacity"), 0, 10);
	_baseAlpha = ALPHA_OPAQUE * opacity / 10;
}

void TouchControls::beforeDraw() {
	if (!_zombieCount) {
		return;
	}
	// We have zombies, force redraw to render fading out
	if (_drawer) {
		_drawer->touchControlNotifyChanged();
	}

	// Check for zombie functions, clear them out if expired
	unsigned int zombieCount = 0;
	uint32 now = g_system->getMillis(true);
	for (uint i = 0; i < kFunctionCount; i++) {
		Function *func = _functions[i];
		if (func->status != kFunctionZombie) {
			continue;
		}
		if (func->lastActivable < now) {
			func->status = kFunctionInactive;
			continue;
		}
		zombieCount++;
	}
	_zombieCount = zombieCount;
}

TouchControls::FunctionId TouchControls::getFunctionId(int x, int y) {
	if (_screen_width == 0) {
		// Avoid divide by 0 error
		return kFunctionNone;
	}

	// Exclude areas reserved for system
	if ((x < _gesturesInsets[0] * SCALE_FACTOR_FXP) ||
		(y < _gesturesInsets[1] * SCALE_FACTOR_FXP) ||
		(x >= (int)_screen_width - _gesturesInsets[2] * SCALE_FACTOR_FXP) ||
		(y >= (int)_screen_height - _gesturesInsets[3] * SCALE_FACTOR_FXP)) {
		return kFunctionNone;
	}

	// Hit-test priority: Start > ScummVM > shoulders > A/B/X/Y hotspots > left
	// region (D-pad) > right region (drag-to-look).
	if (_functions[kFunctionStart]->isInside(x, y)) {
		return kFunctionStart;
	}
	if (_functions[kFunctionScummVM]->isInside(x, y)) {
		return kFunctionScummVM;
	}
	if (_functions[kFunctionShoulderL]->isInside(x, y)) {
		return kFunctionShoulderL;
	}
	if (_functions[kFunctionShoulderR]->isInside(x, y)) {
		return kFunctionShoulderR;
	}
	static const FunctionId abxy[] = {
		kFunctionButtonY, kFunctionButtonB, kFunctionButtonA, kFunctionButtonX
	};
	for (int i = 0; i < ARRAYSIZE(abxy); i++) {
		if (_functions[abxy[i]]->isInside(x, y)) {
			return abxy[i];
		}
	}

	int xRatio = x * 100 / (int)_screen_width;
	if (xRatio < LEFT_REGION_MAX_PCT) {
		return kFunctionDpad;
	}
	return kFunctionLook;
}

void TouchControls::setDrawer(TouchControlsDrawer *drawer, int width, int height) {
	_drawer = drawer;
	_screen_width = width * SCALE_FACTOR_FXP;
	_screen_height = height * SCALE_FACTOR_FXP;

	refreshConfig();

	if (drawer && _svg) {
		drawer->touchControlInitSurface(*_svg);
	}
}

TouchControls::Function *TouchControls::getFunctionFromPointerId(int ptrId) {
	for (uint i = 0; i < kFunctionCount; i++) {
		Function *func = _functions[i];
		if (func->status != kFunctionActive) {
			continue;
		}
		if (func->pointerId == ptrId) {
			return func;
		}
	}
	return nullptr;
}

TouchControls::Function *TouchControls::getZombieFunctionFromPos(int x, int y) {
	if (!_zombieCount) {
		return nullptr;
	}
	for (uint i = 0; i < kFunctionCount; i++) {
		Function *func = _functions[i];
		if (func->status != kFunctionZombie) {
			// Already assigned to a finger or dead
			continue;
		}
		if (func->isInside(x, y)) {
			return func;
		}
	}
	return nullptr;
}

void TouchControls::draw() {
	assert(_drawer != nullptr);

	if (!_svg) {
		return;
	}

	// Hide the entire pad unless the on-screen gamepad is the active touch mode:
	// nothing is drawn in touchpad/mouse mode, when on-screen controls are off,
	// or when a physical controller forced the fallback mode.
	OSystem_SDL *sdlSystem = dynamic_cast<OSystem_SDL *>(g_system);
	if (!sdlSystem || !sdlSystem->hasTouchscreen() ||
			!ConfMan.getBool("onscreen_control") ||
			sdlSystem->getTouchMode() != OSystem_SDL::TOUCH_MODE_GAMEPAD) {
		return;
	}

	uint32 now = g_system->getMillis(true);

	for (uint i = 0; i < kFunctionCount; i++) {
		Function *func = _functions[i];
		uint8 hlAlpha;
		switch (func->status) {
		case kFunctionActive:
			hlAlpha = ALPHA_OPAQUE;
			break;
		case kFunctionZombie:
			if (func->lastActivable < now) {
				hlAlpha = 0;
			} else {
				hlAlpha = (func->lastActivable - now) * ALPHA_OPAQUE / ZOMBIE_TIMEOUT;
			}
			break;
		case kFunctionInactive:
		default:
			hlAlpha = 0;
			break;
		}
		// Resting glyphs are always drawn at the configured opacity; the
		// pressed-state highlight is drawn on top while a finger is down.
		func->draw(_baseAlpha, hlAlpha);
	}
}

void TouchControls::update(Action action, int ptrId, int x, int y) {
	x *= SCALE_FACTOR_FXP;
	y *= SCALE_FACTOR_FXP;
	if (action == kActionDown) {
		Function *func = getZombieFunctionFromPos(x, y);
		if (!func) {
			// Finger was not pressed on a zombie function
			// Determine which function it could be
			FunctionId funcId = getFunctionId(x, y);
			if (funcId != kFunctionNone) {
				func = _functions[funcId];
			}
			if (!func) {
				// No function for this finger
				return;
			}
			if (func->status == kFunctionActive) {
				// Another finger is already on this function
				return;
			}

			// When zombie, we reuse the old start coordinates
			// but not when starting over
			func->reset();
			func->startX = x;
			func->startY = y;
		}


		func->status = kFunctionActive;
		func->pointerId = ptrId;
		func->currentX = x;
		func->currentY = y;

		int dX = x - func->startX;
		int dY = y - func->startY;

		func->touch(dX, dY, action);
		if (_drawer) {
			_drawer->touchControlNotifyChanged();
		}
	} else if (action == kActionMove) {
		Function *func = getFunctionFromPointerId(ptrId);
		if (!func) {
			return;
		}

		func->currentX = x;
		func->currentY = y;

		int dX = x - func->startX;
		int dY = y - func->startY;

		func->touch(dX, dY, action);
		if (_drawer) {
			_drawer->touchControlNotifyChanged();
		}
	} else if (action == kActionUp) {
		Function *func = getFunctionFromPointerId(ptrId);
		if (!func) {
			return;
		}

		func->currentX = x;
		func->currentY = y;

		int dX = x - func->startX;
		int dY = y - func->startY;

		func->touch(dX, dY, action);
		func->status = kFunctionZombie;
		func->lastActivable = g_system->getMillis(true) + ZOMBIE_TIMEOUT;
		if (_drawer) {
			_drawer->touchControlNotifyChanged();
		}
		_zombieCount++;
	} else if (action == kActionCancel) {
		for (uint i = 0; i < kFunctionCount; i++) {
			Function *func = _functions[i];

			if (func->status == kFunctionActive) {
				func->touch(0, 0, action);
			}
			func->reset();
		}
		if (_drawer) {
			_drawer->touchControlNotifyChanged();
		}
		_zombieCount = 0;
	}
}

void TouchControls::buttonDown(Common::JoystickButton jb) {
	if (jb == Common::JOYSTICK_BUTTON_INVALID) {
		return;
	}

	Common::Event ev;
	ev.type = Common::EVENT_JOYBUTTON_DOWN;
	ev.joystick.button = jb;
	dynamic_cast<OSystem_SDL *>(g_system)->pushEvent(ev);
}

void TouchControls::buttonUp(Common::JoystickButton jb) {
	if (jb == Common::JOYSTICK_BUTTON_INVALID) {
		return;
	}

	Common::Event ev;
	ev.type = Common::EVENT_JOYBUTTON_UP;
	ev.joystick.button = jb;
	dynamic_cast<OSystem_SDL *>(g_system)->pushEvent(ev);
}

void TouchControls::axisMotion(Common::JoystickAxis axis, int16 value) {
	Common::Event ev;
	ev.type = Common::EVENT_JOYAXIS_MOTION;
	ev.joystick.axis = axis;
	ev.joystick.position = value;
	dynamic_cast<OSystem_SDL *>(g_system)->pushEvent(ev);
}

void TouchControls::mouseRelative(int relX, int relY) {
	Common::Event ev;
	ev.type = Common::EVENT_MOUSEMOVE;
	// Absolute position is kept at the current cursor; the engine (HPL1) reads
	// the relative delta for the camera.
	ev.mouse = g_system->getEventManager()->getMousePos();
	ev.relMouse = Common::Point(relX, relY);
	dynamic_cast<OSystem_SDL *>(g_system)->pushEvent(ev);
}

void TouchControls::mouseClick() {
	Common::Point p = g_system->getEventManager()->getMousePos();

	Common::Event down;
	down.type = Common::EVENT_LBUTTONDOWN;
	down.mouse = p;
	dynamic_cast<OSystem_SDL *>(g_system)->pushEvent(down);

	Common::Event up;
	up.type = Common::EVENT_LBUTTONUP;
	up.mouse = p;
	dynamic_cast<OSystem_SDL *>(g_system)->pushEvent(up);
}

void TouchControls::mainMenu() {
	// Open the ScummVM Global Main Menu. EVENT_MAINMENU is handled by the default
	// event manager, which calls Engine::openMainMenuDialog() (the GMM). Pushed
	// through the same event queue as the toggle long-press EVENT_VIRTUAL_KEYBOARD.
	Common::Event ev;
	ev.type = Common::EVENT_MAINMENU;
	dynamic_cast<OSystem_SDL *>(g_system)->pushEvent(ev);
}

void TouchControls::drawSurface(uint8 alpha, int x, int y, int offX, int offY, const Common::Rect &clip) const {
	if (!alpha) {
		return;
	}
	Common::Rect clip_(SVG_PIXELS(clip.left), SVG_PIXELS(clip.top),
			SVG_PIXELS(clip.right), SVG_PIXELS(clip.bottom));
	_drawer->touchControlDraw(alpha,
			SCALED_PIXELS(x + SVG_SCALED(offX)), SCALED_PIXELS(y + SVG_SCALED(offY)),
			clip_.width(), clip_.height(), clip_);
}

// ===========================================================================
// Floating D-pad (move / DPAD_UP/DOWN/LEFT/RIGHT buttons)
// ===========================================================================

bool TouchControls::FunctionDpad::isInside(int x, int y) {
	int dX = x - startX;
	int dY = y - startY;
	unsigned int sqNorm = (unsigned int)(dX * dX) + (unsigned int)(dY * dY);
	if (sqNorm <= FUNC_SVG_SQ_SCALED(DPAD_HIT)) {
		return true;
	}
	// Also accept touches near the last finger position (zombie re-touch).
	dX = x - currentX;
	dY = y - currentY;
	sqNorm = (unsigned int)(dX * dX) + (unsigned int)(dY * dY);
	return sqNorm <= FUNC_SVG_SQ_SCALED(DPAD_DEAD);
}

uint32 TouchControls::FunctionDpad::directionMask(int dX, int dY) const {
	uint32 newMask = 0;

	unsigned int sqNorm = (unsigned int)(dX * dX) + (unsigned int)(dY * dY);
	if (sqNorm >= FUNC_SVG_SQ_SCALED(DPAD_DEAD)) {
		// We are far enough from the centre. 8-way octant test:
		//   Left/right sensitive zone: |dY| <= |dX| * tan(60deg) (sqrt(3))
		//   Up/down   sensitive zone: |dY| >= |dX| * tan(30deg) (1/sqrt(3))
		unsigned int sq3 = abs(dX) * 51409 / 29681;
		unsigned int isq3 = abs(dX) * 29681 / 51409;
		unsigned int adY = abs(dY);

		if (adY <= sq3) {
			// Left or right (bit 3 / bit 1).
			newMask |= (dX < 0) ? 8 : 2;
		}
		if (adY >= isq3) {
			// Up or down (bit 0 / bit 2).
			newMask |= (dY < 0) ? 1 : 4;
		}
	}

	return newMask;
}

void TouchControls::FunctionDpad::maskToDpadButtons(uint32 oldMask, uint32 newMask) {
	static const Common::JoystickButton buttons[] = {
		Common::JOYSTICK_BUTTON_DPAD_UP, Common::JOYSTICK_BUTTON_DPAD_RIGHT,
		Common::JOYSTICK_BUTTON_DPAD_DOWN, Common::JOYSTICK_BUTTON_DPAD_LEFT
	};

	uint32 diff = newMask ^ oldMask;

	for (int i = 0, m = 1; i < ARRAYSIZE(buttons); i++, m <<= 1) {
		if ((diff & m) && (oldMask & m)) {
			TouchControls::buttonUp(buttons[i]);
		}
	}
	for (int i = 0, m = 1; i < ARRAYSIZE(buttons); i++, m <<= 1) {
		if ((diff & m) && (newMask & m)) {
			TouchControls::buttonDown(buttons[i]);
		}
	}
}

void TouchControls::FunctionDpad::touch(int dX, int dY, Action action) {
	uint32 newMask = 0;
	if (action != kActionCancel && action != kActionUp) {
		newMask = directionMask(dX, dY);
	}
	if (newMask != mask) {
		maskToDpadButtons(mask, newMask);
		mask = newMask;
	}
}

void TouchControls::FunctionDpad::draw(uint8 baseAlpha, uint8 hlAlpha) {
	bool active = (status != kFunctionInactive);
	int cx = active ? startX : parent->homeX(DIR_HOME_X_PCT);
	int cy = active ? startY : parent->homeY(DIR_HOME_Y_PCT);

	// Base: D-pad cell (circle + grey arrows).
	{
		Common::Rect clip(DPAD_BASE_CLIP);
		parent->drawSurface(baseAlpha, cx, cy, -clip.width() / 2, -clip.height() / 2, clip);
	}

	if (!hlAlpha || !mask) {
		return;
	}

	// Pressed arrow highlights from col 0 row 1.
	if (mask & 1) { // up
		Common::Rect clip(0, DPAD_HL_TOP, DPAD_ARROW_W, DPAD_HL_TOP + DPAD_ARROW_H);
		parent->drawSurface(hlAlpha, cx, cy, -clip.width() / 2, -clip.height(), clip);
	}
	if (mask & 2) { // right
		Common::Rect clip(DPAD_ARROW_W, DPAD_HL_TOP, 2 * DPAD_ARROW_W, DPAD_HL_TOP + DPAD_ARROW_H);
		parent->drawSurface(hlAlpha, cx, cy, 0, -clip.height() / 2, clip);
	}
	if (mask & 4) { // down
		Common::Rect clip(0, DPAD_HL_TOP + DPAD_ARROW_H, DPAD_ARROW_W, DPAD_HL_TOP + 2 * DPAD_ARROW_H);
		parent->drawSurface(hlAlpha, cx, cy, -clip.width() / 2, 0, clip);
	}
	if (mask & 8) { // left
		Common::Rect clip(DPAD_ARROW_W, DPAD_HL_TOP + DPAD_ARROW_H, 2 * DPAD_ARROW_W, DPAD_HL_TOP + 2 * DPAD_ARROW_H);
		parent->drawSurface(hlAlpha, cx, cy, -clip.width(), -clip.height() / 2, clip);
	}
}

// ===========================================================================
// ScummVM button (top-left, opens the Global Main Menu)
// ===========================================================================

bool TouchControls::FunctionScummVM::isInside(int x, int y) {
	int cx = parent->homeX(SCUMM_HOME_X_PCT);
	int cy = parent->homeY(SCUMM_HOME_Y_PCT);
	int dX = x - cx;
	int dY = y - cy;
	unsigned int sqNorm = (unsigned int)(dX * dX) + (unsigned int)(dY * dY);
	return sqNorm <= FUNC_SVG_SQ_SCALED(SCUMM_HIT);
}

void TouchControls::FunctionScummVM::touch(int dX, int dY, Action action) {
	// Fire the GMM once on press; the press visual lasts until the finger lifts.
	bool wantPressed = (action != kActionCancel && action != kActionUp);
	if (wantPressed != pressed) {
		pressed = wantPressed;
		if (pressed) {
			mainMenu();
		}
	}
}

void TouchControls::FunctionScummVM::draw(uint8 baseAlpha, uint8 hlAlpha) {
	int cx = parent->homeX(SCUMM_HOME_X_PCT);
	int cy = parent->homeY(SCUMM_HOME_Y_PCT);

	Common::Rect clip(SCUMM_CLIP);
	parent->drawSurface(baseAlpha, cx, cy, -clip.width() / 2, -clip.height() / 2, clip);
	if (hlAlpha) {
		// Brighten the badge while pressed.
		parent->drawSurface(hlAlpha, cx, cy, -clip.width() / 2, -clip.height() / 2, clip);
	}
}

// ===========================================================================
// A/B/X/Y face buttons (independent diamond hotspots)
// ===========================================================================

void TouchControls::FunctionButton::center(int &cx, int &cy) const {
	cx = parent->homeX(ABXY_HOME_X_PCT) + SVG_UNSCALED(_dxSvg) * (int)parent->_scale;
	cy = parent->homeY(ABXY_HOME_Y_PCT) + SVG_UNSCALED(_dySvg) * (int)parent->_scale;
}

bool TouchControls::FunctionButton::isInside(int x, int y) {
	int cx, cy;
	center(cx, cy);
	int dX = x - cx;
	int dY = y - cy;
	unsigned int sqNorm = (unsigned int)(dX * dX) + (unsigned int)(dY * dY);
	return sqNorm <= FUNC_SVG_SQ_SCALED(ABXY_HIT);
}

void TouchControls::FunctionButton::touch(int dX, int dY, Action action) {
	bool wantPressed = (action != kActionCancel && action != kActionUp);
	if (wantPressed != pressed) {
		pressed = wantPressed;
		if (pressed) {
			buttonDown(_jb);
		} else {
			buttonUp(_jb);
		}
	}
}

void TouchControls::FunctionButton::draw(uint8 baseAlpha, uint8 hlAlpha) {
	int cx, cy;
	center(cx, cy);

	parent->drawSurface(baseAlpha, cx, cy, -_restClip.width() / 2, -_restClip.height() / 2, _restClip);
	if (hlAlpha && pressed) {
		parent->drawSurface(hlAlpha, cx, cy, -_hlClip.width() / 2, -_hlClip.height() / 2, _hlClip);
	}
}

// ===========================================================================
// L1 / R1 shoulder buttons (top corners, labelled pills)
// ===========================================================================

bool TouchControls::FunctionShoulder::isInside(int x, int y) {
	int cx = parent->homeX(_right ? SHOULDER_R_X_PCT : SHOULDER_L_X_PCT);
	int cy = parent->homeY(SHOULDER_Y_PCT);
	int dX = x - cx;
	int dY = y - cy;
	unsigned int sqNorm = (unsigned int)(dX * dX) + (unsigned int)(dY * dY);
	return sqNorm <= FUNC_SVG_SQ_SCALED(SHOULDER_HIT);
}

void TouchControls::FunctionShoulder::touch(int dX, int dY, Action action) {
	bool wantPressed = (action != kActionCancel && action != kActionUp);
	if (wantPressed != pressed) {
		pressed = wantPressed;
		if (pressed) {
			buttonDown(_jb);
		} else {
			buttonUp(_jb);
		}
	}
}

void TouchControls::FunctionShoulder::draw(uint8 baseAlpha, uint8 hlAlpha) {
	int cx = parent->homeX(_right ? SHOULDER_R_X_PCT : SHOULDER_L_X_PCT);
	int cy = parent->homeY(SHOULDER_Y_PCT);

	Common::Rect clip = _right ? Common::Rect(R1_CLIP) : Common::Rect(L1_CLIP);
	parent->drawSurface(baseAlpha, cx, cy, -clip.width() / 2, -clip.height() / 2, clip);
	if (hlAlpha) {
		// Brighten the pill while pressed.
		parent->drawSurface(hlAlpha, cx, cy, -clip.width() / 2, -clip.height() / 2, clip);
	}
}

// ===========================================================================
// Start / menu button (top centre)
// ===========================================================================

bool TouchControls::FunctionStart::isInside(int x, int y) {
	int cx = parent->homeX(START_HOME_X_PCT);
	int cy = parent->homeY(START_HOME_Y_PCT);
	int dX = x - cx;
	int dY = y - cy;
	unsigned int sqNorm = (unsigned int)(dX * dX) + (unsigned int)(dY * dY);
	return sqNorm <= FUNC_SVG_SQ_SCALED(START_HIT);
}

void TouchControls::FunctionStart::touch(int dX, int dY, Action action) {
	// Digital, like the face buttons: pressed while a finger is down.
	bool wantPressed = (action != kActionCancel && action != kActionUp);
	if (wantPressed != pressed) {
		pressed = wantPressed;
		if (pressed) {
			buttonDown(Common::JOYSTICK_BUTTON_START);
		} else {
			buttonUp(Common::JOYSTICK_BUTTON_START);
		}
	}
}

void TouchControls::FunctionStart::draw(uint8 baseAlpha, uint8 hlAlpha) {
	int cx = parent->homeX(START_HOME_X_PCT);
	int cy = parent->homeY(START_HOME_Y_PCT);

	Common::Rect clip(START_CLIP);
	parent->drawSurface(baseAlpha, cx, cy, -clip.width() / 2, -clip.height() / 2, clip);
	if (hlAlpha) {
		// Brighten the glyph while pressed.
		parent->drawSurface(hlAlpha, cx, cy, -clip.width() / 2, -clip.height() / 2, clip);
	}
}

// ===========================================================================
// Drag-to-look (right half, invisible: relative-mouse camera + tap-to-click)
// ===========================================================================

void TouchControls::FunctionLook::touch(int dX, int dY, Action action) {
	if (action == kActionDown) {
		prevX = currentX;
		prevY = currentY;
		hiresX = hiresY = 0.0f;
		maxTravel = 0;
		return;
	}

	if (action == kActionCancel) {
		return;
	}

	// Track the largest travel from the press origin (window px) for the
	// tap-to-click decision (Manhattan distance is plenty here).
	int travel = (abs(dX) + abs(dY)) / SCALE_FACTOR_FXP;
	if (travel > maxTravel) {
		maxTravel = travel;
	}

	if (action == kActionUp) {
		if (maxTravel <= TAP_MAX_TRAVEL) {
			mouseClick();
		}
		return;
	}

	// kActionMove: convert per-frame finger delta into relative-mouse motion,
	// mirroring the touchpad relmouse scaling in sdl2-events.cpp.
	int ddX = (int)currentX - prevX;
	int ddY = (int)currentY - prevY;
	prevX = currentX;
	prevY = currentY;

	int kbdMouseSpeed = CLIP<int>(ConfMan.getInt("kbdmouse_speed"), 0, 7);
	float speedFactor = (kbdMouseSpeed + 1) * 0.25f;

	hiresX += (ddX / (float)SCALE_FACTOR_FXP) * 1.25f * speedFactor;
	hiresY += (ddY / (float)SCALE_FACTOR_FXP) * 1.25f * speedFactor;

	int relX = (int)hiresX;
	int relY = (int)hiresY;
	hiresX -= relX;
	hiresY -= relY;

	if (relX || relY) {
		mouseRelative(relX, relY);
	}
}
