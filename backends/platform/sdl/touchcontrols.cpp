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
//   col 2 row 0 (256,0)   : "system" cluster - GUIDE (top), RS (right), the
//                           silver Start/menu lens (bottom), LS (left)
//   col 2 row 1 (256,128) : system pressed highlights (packed in quadrants)
//
// A spare row is appended below the 384x256 sheet (SVG y 256..384) into which
// the drag-to-look ring/knob are drawn from primitives at load time (see
// renderPrimitives()).
// ---------------------------------------------------------------------------
#define SVG_WIDTH          384
#define SVG_HEIGHT         256
// Total height of the backing surface: the sheet plus the spare primitives row
// (the drag-to-look ring/knob are drawn into it from primitives).
#define SVG_FULL_HEIGHT    384

// ---------------------------------------------------------------------------
// Glyph sizes, as a percentage of the gamepad.svg native (1.5x) glyph size.
// Grouped and tunable: the primary controls (D-pad, A/B/X/Y) are drawn smaller
// than the old 100% so they sit comfortably under the thumbs, and the secondary
// top-row controls (L1/R1, Start, ScummVM) smaller still so they clearly read
// as secondary.
// ---------------------------------------------------------------------------
#define DPAD_SIZE_PCT        68
#define ABXY_SIZE_PCT        68
// Top (system) cluster: rendered at 80% of the primary D-pad / A-B-X-Y clusters.
#define SYS_SIZE_PCT         (ABXY_SIZE_PCT * 80 / 100)   // ~54

// A/B/X/Y: the whole resting cluster glyph (col 1 row 0) is drawn once, cleanly
// (no per-button cropping), and a per-button pressed-highlight circle (col 1
// row 1) is drawn on top of whichever button(s) are held. This keeps the four
// independent multi-touch hotspots while rendering uncut glyphs.
#define ABXY_CLUSTER_CLIP   128,   0, 256, 128
#define ABXY_HL_Y           128, 128, 192, 192
#define ABXY_HL_B           192, 128, 256, 192
#define ABXY_HL_A           128, 192, 192, 256
#define ABXY_HL_X           192, 192, 256, 256

// Letter spacing from the cluster centre (SVG px, matches the cluster art) and
// the per-button hit radius (SVG px). The hotspots sit on the letters; the
// radius is kept just under half the diagonal letter spacing so the four
// hotspots stay independent (multi-touch).
#define ABXY_GLYPH_OFFSET   36
#define ABXY_HIT            18

// Floating D-pad geometry (SVG px). The base cell fills a 128 cell; the pressed
// arrow highlights are 64x64 crops of the highlight row (col 0 row 1).
#define DPAD_BASE_CLIP        0,   0, 128, 128
#define DPAD_ARROW_W        64
#define DPAD_ARROW_H        64
#define DPAD_HL_TOP        128
#define DPAD_DEAD           27   // direction dead-zone (SVG px); larger = less twitchy walking
#define DPAD_HIT            62   // hit radius for zombie re-touch / inside test

// Drag-to-look indicator (drawn from primitives into the spare row): a ring
// ("base", anchored at the finger-down position) and a filled dot ("knob", at
// the current finger position). Sizes in SVG px.
#define LOOK_RING_RADIUS    34
#define LOOK_DOT_RADIUS     13
#define LOOK_RING_CLIP       60, 256, 60 + 2 * LOOK_RING_RADIUS, 256 + 2 * LOOK_RING_RADIUS
#define LOOK_DOT_CLIP       150, 256, 150 + 2 * LOOK_DOT_RADIUS, 256 + 2 * LOOK_DOT_RADIUS

#define ALPHA_OPAQUE       255
#define ZOMBIE_TIMEOUT     500 // ms

// Drag-to-look: a finger that lifts having travelled less than this (window px)
// synthesizes a left click instead.
#define TAP_MAX_TRAVEL       8

// Drag-to-look feel (tunable). The per-frame finger delta is turned into
// relative-mouse motion as:  relMouse = delta * BASE * (1 + ACCEL * speed),
// where speed is the per-frame finger travel magnitude in window pixels. BASE
// is ~2x the previous sensitivity; ACCEL makes fast flicks move the camera
// disproportionately more than slow drags.
#define LOOK_BASE_SENS       2.5f
#define LOOK_ACCEL           0.04f

// ---------------------------------------------------------------------------
// Home (resting) positions of the controls, in percent of the window. AAA
// touch-FPS layout in landscape: D-pad bottom-left, A/B/X/Y diamond
// bottom-right, and the gamepad.svg "system" cluster top-centre (a diamond of
// GMM/R1/Start/L1 on its grouping circle). An invisible drag-to-look region
// fills the rest of the right half. (The mode-toggle icon lives in the top-right
// corner and is drawn elsewhere.)
// ---------------------------------------------------------------------------
#define DIR_HOME_X_PCT       17
#define DIR_HOME_Y_PCT       76
#define ABXY_HOME_X_PCT      85
#define ABXY_HOME_Y_PCT      76

// Secondary top-centre cluster: the gamepad.svg "system" cluster cell (col 2),
// a translucent grouping circle with four buttons in a diamond - GUIDE (top),
// RS (right), the silver Start/menu lens (bottom) and LS (left). Rendered like
// the A/B/X/Y cluster: the whole resting cell is drawn once, with a per-button
// pressed highlight from the col-2 highlight row overlaid on press. The diamond
// positions map to the existing top functions:
//   top    (GUIDE glyph)  -> ScummVM / GMM button
//   right  (RS glyph)     -> R1  (right shoulder)
//   bottom (Start lens)   -> Start
//   left   (LS glyph)     -> L1  (left shoulder)
#define AUX_HOME_X_PCT       50
#define AUX_HOME_Y_PCT       13
// Diamond offset of each button from the cluster centre (SVG px), matching the
// cluster art (buttons drawn at +/-36 in the 128 cell, radius ~24).
#define AUX_GLYPH_OFFSET     36

// Resting cell (col 2 row 0) and the per-button pressed-highlight crops (col 2
// row 1), packed in quadrants exactly like the A/B/X/Y highlight row.
#define SYS_CLUSTER_CLIP    256,   0, 384, 128
#define SYS_HL_GUIDE        256, 128, 320, 192   // top    -> GMM
#define SYS_HL_RS           320, 128, 384, 192   // right  -> R1
#define SYS_HL_START        256, 192, 320, 256   // bottom -> Start
#define SYS_HL_LS           320, 192, 384, 256   // left   -> L1

// Per-button hit radius (SVG px). Scaled by the cluster size (like the button
// positions) so it tracks the drawn button radius (~24 in the cell) and the four
// diamond hotspots stay distinct as the cluster size changes.
#define SYS_HIT             (26 * SYS_SIZE_PCT / 100)

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
	// The first face button owns the shared resting-cluster glyph (drawn once);
	// all four still have independent hotspots and pressed highlights. The last
	// two args are the highlight crop offset (in half-widths/heights) from the
	// cluster centre, matching the colour circles of the highlight row.
	_functions[kFunctionButtonY]   = new FunctionButton(this, Common::JOYSTICK_BUTTON_Y,
			0, -ABXY_GLYPH_OFFSET, Common::Rect(ABXY_HL_Y), -1, -2, true);
	_functions[kFunctionButtonB]   = new FunctionButton(this, Common::JOYSTICK_BUTTON_B,
			ABXY_GLYPH_OFFSET, 0, Common::Rect(ABXY_HL_B), 0, -1, false);
	_functions[kFunctionButtonA]   = new FunctionButton(this, Common::JOYSTICK_BUTTON_A,
			0, ABXY_GLYPH_OFFSET, Common::Rect(ABXY_HL_A), -1, 0, false);
	_functions[kFunctionButtonX]   = new FunctionButton(this, Common::JOYSTICK_BUTTON_X,
			-ABXY_GLYPH_OFFSET, 0, Common::Rect(ABXY_HL_X), -2, -1, false);
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

	// --- Drag-to-look indicator: a ring ("base") and a filled dot ("knob") ---
	const uint32 ringCol = fmt.ARGBToColor(210, 240, 240, 240);
	const uint32 dotCol  = fmt.ARGBToColor(255, 255, 255, 255); // brighter knob
	{
		Common::Rect c(LOOK_RING_CLIP);
		Common::Rect r(SVG_PIXELS(c.left), SVG_PIXELS(c.top),
				SVG_PIXELS(c.right), SVG_PIXELS(c.bottom));
		int cx = (r.left + r.right) / 2;
		int cy = (r.top + r.bottom) / 2;
		int rOut = r.width() / 2 - 1;
		// A few concentric outlines give the ring some thickness.
		int thick = MAX(2, r.width() / 14);
		for (int t = 0; t < thick; t++) {
			_svg->drawEllipse(cx - rOut + t, cy - rOut + t,
					cx + rOut - t, cy + rOut - t, ringCol, false);
		}
	}
	{
		Common::Rect c(LOOK_DOT_CLIP);
		Common::Rect r(SVG_PIXELS(c.left), SVG_PIXELS(c.top),
				SVG_PIXELS(c.right), SVG_PIXELS(c.bottom));
		int cx = (r.left + r.right) / 2;
		int cy = (r.top + r.bottom) / 2;
		int rOut = r.width() / 2 - 1;
		_svg->drawEllipse(cx - rOut, cy - rOut, cx + rOut, cy + rOut, dotCol, true);
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
			!ConfMan.getBool(ONSCREEN_CONTROL_KEY) ||
			sdlSystem->getTouchMode() != Common::kTouchModeGamepad) {
		return;
	}

	uint32 now = g_system->getMillis(true);

	// Draw the top (system) cluster resting cell once - the grouping circle plus
	// the GUIDE/RS/Start/LS diamond - just like the A/B/X/Y cluster owner does.
	// Each top function overlays its own pressed highlight on top while held.
	{
		Common::Rect clip(SYS_CLUSTER_CLIP);
		drawSurface(_baseAlpha, homeX(AUX_HOME_X_PCT), homeY(AUX_HOME_Y_PCT),
				-clip.width() / 2, -clip.height() / 2, clip, SYS_SIZE_PCT);
	}

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

void TouchControls::keyDown(Common::KeyCode kc) {
	Common::Event ev;
	ev.type = Common::EVENT_KEYDOWN;
	ev.kbd = Common::KeyState(kc);
	dynamic_cast<OSystem_SDL *>(g_system)->pushEvent(ev);
}

void TouchControls::keyUp(Common::KeyCode kc) {
	Common::Event ev;
	ev.type = Common::EVENT_KEYUP;
	ev.kbd = Common::KeyState(kc);
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

void TouchControls::drawSurface(uint8 alpha, int x, int y, int offX, int offY, const Common::Rect &clip, int sizePct) const {
	if (!alpha) {
		return;
	}
	Common::Rect clip_(SVG_PIXELS(clip.left), SVG_PIXELS(clip.top),
			SVG_PIXELS(clip.right), SVG_PIXELS(clip.bottom));
	// Destination size = source crop scaled by sizePct (drawTexture/RenderCopy
	// stretch the crop). The SVG-px offsets are scaled identically so centring
	// math (e.g. -clip.width()/2) keeps centring the scaled glyph.
	int w = clip_.width() * sizePct / 100;
	int h = clip_.height() * sizePct / 100;
	// The SVG-px offsets are usually negative (centring, e.g. -clip.width()/2).
	// _scale is unsigned, so the offset must be scaled with explicit signed
	// arithmetic: a negative value multiplied by the unsigned _scale wraps to a
	// huge positive number, and dividing that here (rather than letting it cancel
	// in a later add) would push the glyph millions of pixels off-screen.
	int offXpx = SVG_UNSCALED(offX) * (int)_scale / SCALE_FACTOR_FXP;
	int offYpx = SVG_UNSCALED(offY) * (int)_scale / SCALE_FACTOR_FXP;
	int dx = SCALED_PIXELS(x) + offXpx * sizePct / 100;
	int dy = SCALED_PIXELS(y) + offYpx * sizePct / 100;
	_drawer->touchControlDraw(alpha, dx, dy, w, h, clip_);
}

void TouchControls::auxCenter(int dxSvg, int dySvg, int &cx, int &cy) const {
	// Diamond offsets scaled like the cluster cell so the hotspots sit on the
	// drawn buttons (mirrors FunctionButton::center for the A/B/X/Y cluster).
	cx = homeX(AUX_HOME_X_PCT) + SVG_UNSCALED(dxSvg) * (int)_scale * SYS_SIZE_PCT / 100;
	cy = homeY(AUX_HOME_Y_PCT) + SVG_UNSCALED(dySvg) * (int)_scale * SYS_SIZE_PCT / 100;
}

void TouchControls::drawSysHighlight(uint8 hlAlpha, const Common::Rect &clip, int hlMulX, int hlMulY) const {
	drawSurface(hlAlpha, homeX(AUX_HOME_X_PCT), homeY(AUX_HOME_Y_PCT),
			hlMulX * clip.width() / 2, hlMulY * clip.height() / 2, clip, SYS_SIZE_PCT);
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
		parent->drawSurface(baseAlpha, cx, cy, -clip.width() / 2, -clip.height() / 2, clip, DPAD_SIZE_PCT);
	}

	if (!hlAlpha || !mask) {
		return;
	}

	// Pressed arrow highlights from col 0 row 1.
	if (mask & 1) { // up
		Common::Rect clip(0, DPAD_HL_TOP, DPAD_ARROW_W, DPAD_HL_TOP + DPAD_ARROW_H);
		parent->drawSurface(hlAlpha, cx, cy, -clip.width() / 2, -clip.height(), clip, DPAD_SIZE_PCT);
	}
	if (mask & 2) { // right
		Common::Rect clip(DPAD_ARROW_W, DPAD_HL_TOP, 2 * DPAD_ARROW_W, DPAD_HL_TOP + DPAD_ARROW_H);
		parent->drawSurface(hlAlpha, cx, cy, 0, -clip.height() / 2, clip, DPAD_SIZE_PCT);
	}
	if (mask & 4) { // down
		Common::Rect clip(0, DPAD_HL_TOP + DPAD_ARROW_H, DPAD_ARROW_W, DPAD_HL_TOP + 2 * DPAD_ARROW_H);
		parent->drawSurface(hlAlpha, cx, cy, -clip.width() / 2, 0, clip, DPAD_SIZE_PCT);
	}
	if (mask & 8) { // left
		Common::Rect clip(DPAD_ARROW_W, DPAD_HL_TOP + DPAD_ARROW_H, 2 * DPAD_ARROW_W, DPAD_HL_TOP + 2 * DPAD_ARROW_H);
		parent->drawSurface(hlAlpha, cx, cy, -clip.width(), -clip.height() / 2, clip, DPAD_SIZE_PCT);
	}
}

// ===========================================================================
// ScummVM / GMM button (GUIDE glyph, top of the system cluster diamond)
// ===========================================================================

bool TouchControls::FunctionScummVM::isInside(int x, int y) {
	int cx, cy;
	parent->auxCenter(0, -AUX_GLYPH_OFFSET, cx, cy); // top
	int dX = x - cx;
	int dY = y - cy;
	unsigned int sqNorm = (unsigned int)(dX * dX) + (unsigned int)(dY * dY);
	return sqNorm <= FUNC_SVG_SQ_SCALED(SYS_HIT);
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

void TouchControls::FunctionScummVM::draw(uint8 /*baseAlpha*/, uint8 hlAlpha) {
	// Resting art is the shared system-cluster cell (drawn once in draw()); this
	// only overlays the GUIDE pressed highlight while held (top -> hlMul -1,-2).
	if (hlAlpha && pressed) {
		parent->drawSysHighlight(hlAlpha, Common::Rect(SYS_HL_GUIDE), -1, -2);
	}
}

// ===========================================================================
// A/B/X/Y face buttons (independent diamond hotspots)
// ===========================================================================

void TouchControls::FunctionButton::center(int &cx, int &cy) const {
	// The hotspots sit on the letters of the (scaled) resting cluster glyph.
	cx = parent->homeX(ABXY_HOME_X_PCT) + SVG_UNSCALED(_dxSvg) * (int)parent->_scale * ABXY_SIZE_PCT / 100;
	cy = parent->homeY(ABXY_HOME_Y_PCT) + SVG_UNSCALED(_dySvg) * (int)parent->_scale * ABXY_SIZE_PCT / 100;
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
			// The launcher and the GMM are enumerable UIs navigated keyboard-style:
			// the D-pad moves the highlight (JOY_*->arrows) and the A button
			// confirms by activating the focused widget, i.e. Enter - rather than a
			// click-at-cursor. Decide this once, at press time, so the matching
			// release sends the same kind of event (avoids a stuck key/button when
			// activating launches a game and hides the overlay before the finger
			// lifts). In-game the A button stays a normal gamepad A.
			_menuActivate = (_jb == Common::JOYSTICK_BUTTON_A) && g_system->isOverlayVisible();
			if (_menuActivate) {
				keyDown(Common::KEYCODE_RETURN);
			} else {
				buttonDown(_jb);
			}
		} else {
			if (_menuActivate) {
				keyUp(Common::KEYCODE_RETURN);
			} else {
				buttonUp(_jb);
			}
		}
	}
}

void TouchControls::FunctionButton::draw(uint8 baseAlpha, uint8 hlAlpha) {
	// The whole resting cluster glyph is drawn once (by the cluster owner),
	// cleanly and uncut, centred on the cluster home position.
	int ccx = parent->homeX(ABXY_HOME_X_PCT);
	int ccy = parent->homeY(ABXY_HOME_Y_PCT);

	if (_clusterOwner) {
		Common::Rect clip(ABXY_CLUSTER_CLIP);
		parent->drawSurface(baseAlpha, ccx, ccy, -clip.width() / 2, -clip.height() / 2, clip, ABXY_SIZE_PCT);
	}

	// Per-button pressed highlight: the matching colour circle of the highlight
	// row, placed (relative to the cluster centre) on top of this button.
	if (hlAlpha && pressed) {
		Common::Rect clip(_hlClip);
		parent->drawSurface(hlAlpha, ccx, ccy,
				_hlMulX * clip.width() / 2, _hlMulY * clip.height() / 2, clip, ABXY_SIZE_PCT);
	}
}

// ===========================================================================
// L1 / R1 shoulder buttons (top corners, labelled pills)
// ===========================================================================

bool TouchControls::FunctionShoulder::isInside(int x, int y) {
	int cx, cy;
	// R1 on the right, L1 on the left of the diamond.
	parent->auxCenter(_right ? AUX_GLYPH_OFFSET : -AUX_GLYPH_OFFSET, 0, cx, cy);
	int dX = x - cx;
	int dY = y - cy;
	unsigned int sqNorm = (unsigned int)(dX * dX) + (unsigned int)(dY * dY);
	return sqNorm <= FUNC_SVG_SQ_SCALED(SYS_HIT);
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

void TouchControls::FunctionShoulder::draw(uint8 /*baseAlpha*/, uint8 hlAlpha) {
	// Resting art is the shared system-cluster cell; overlay the pressed highlight
	// only: R1 = RS on the right (hlMul 0,-1), L1 = LS on the left (hlMul -2,-1).
	if (hlAlpha && pressed) {
		if (_right) {
			parent->drawSysHighlight(hlAlpha, Common::Rect(SYS_HL_RS), 0, -1);
		} else {
			parent->drawSysHighlight(hlAlpha, Common::Rect(SYS_HL_LS), -2, -1);
		}
	}
}

// ===========================================================================
// Start button (silver lens, bottom of the system cluster diamond)
// ===========================================================================

bool TouchControls::FunctionStart::isInside(int x, int y) {
	int cx, cy;
	parent->auxCenter(0, AUX_GLYPH_OFFSET, cx, cy); // bottom
	int dX = x - cx;
	int dY = y - cy;
	unsigned int sqNorm = (unsigned int)(dX * dX) + (unsigned int)(dY * dY);
	return sqNorm <= FUNC_SVG_SQ_SCALED(SYS_HIT);
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

void TouchControls::FunctionStart::draw(uint8 /*baseAlpha*/, uint8 hlAlpha) {
	// Resting art is the shared system-cluster cell; overlay the Start pressed
	// highlight only (bottom -> hlMul -1,0).
	if (hlAlpha && pressed) {
		parent->drawSysHighlight(hlAlpha, Common::Rect(SYS_HL_START), -1, 0);
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
	// mirroring the touchpad relmouse scaling in sdl2-events.cpp, but with a
	// higher base sensitivity and speed-based acceleration so fast flicks turn
	// the camera disproportionately more than slow drags.
	int ddX = (int)currentX - prevX;
	int ddY = (int)currentY - prevY;
	prevX = currentX;
	prevY = currentY;

	int kbdMouseSpeed = CLIP<int>(ConfMan.getInt("kbdmouse_speed"), 0, 7);
	float speedFactor = (kbdMouseSpeed + 1) * 0.25f;

	// Per-frame finger delta and travel magnitude, in window pixels.
	float dxPx = ddX / (float)SCALE_FACTOR_FXP;
	float dyPx = ddY / (float)SCALE_FACTOR_FXP;
	float fingerSpeed = sqrtf(dxPx * dxPx + dyPx * dyPx);
	float gain = LOOK_BASE_SENS * speedFactor * (1.0f + LOOK_ACCEL * fingerSpeed);

	hiresX += dxPx * gain;
	hiresY += dyPx * gain;

	int relX = (int)hiresX;
	int relY = (int)hiresY;
	hiresX -= relX;
	hiresY -= relY;

	if (relX || relY) {
		mouseRelative(relX, relY);
	}
}

void TouchControls::FunctionLook::draw(uint8 baseAlpha, uint8 /*hlAlpha*/) {
	// Only while a finger is actively dragging the look region: a ring anchored
	// at the finger-down position plus a filled dot at the current finger
	// position, so the offset shows look direction/speed. Gone on release.
	if (status != kFunctionActive) {
		return;
	}

	// The knob is drawn a little brighter than the configured pad opacity.
	uint8 dotAlpha = (uint8)MIN<int>(ALPHA_OPAQUE, baseAlpha + (ALPHA_OPAQUE - baseAlpha) / 2);

	Common::Rect ring(LOOK_RING_CLIP);
	parent->drawSurface(baseAlpha, startX, startY, -ring.width() / 2, -ring.height() / 2, ring);

	Common::Rect dot(LOOK_DOT_CLIP);
	parent->drawSurface(dotAlpha, currentX, currentY, -dot.width() / 2, -dot.height() / 2, dot);
}
