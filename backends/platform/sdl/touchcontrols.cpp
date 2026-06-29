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

// ---------------------------------------------------------------------------
// gamepad.svg sprite-sheet layout (in SVG pixels, 384x256, three 128x128
// columns each with a "resting" row and a "pressed" highlight row):
//   col 0 row 0 (0,0)     : D-pad base (circle + grey arrows)
//   col 0 row 1 (0,128)   : D-pad pressed arrows (white)
//   col 1 row 0 (128,0)   : A/B/X/Y resting cluster
//   col 1 row 1 (128,128) : A/B/X/Y pressed highlights
//   col 2 row 0 (256,0)   : labelled circles (reused for shoulder glyphs)
//   col 2 row 1 (256,128) : (system highlights, unused here)
// ---------------------------------------------------------------------------
#define SVG_WIDTH          384
#define SVG_HEIGHT         256
#define BG_WIDTH           128
#define BG_HEIGHT          128
#define ARROW_DEAD          21
#define ARROW_WIDTH         64
#define ARROW_HEIGHT        64
#define ARROW_HL_TOP       128
#define BUTTON_DEAD         12
#define BUTTON_END          64
#define BUTTON_WIDTH        64
#define BUTTON_HEIGHT       64
#define BUTTON_HL_LEFT     128
#define BUTTON_HL_TOP      128

// Shoulder glyphs are reused from the labelled circles of column 2 (the "LS"
// and "RS" circles), each a 48x48 region centred on its label.
#define SHOULDER_GLYPH      48
#define SHOULDER_L_CLIP     260, 40, 308, 88
#define SHOULDER_R_CLIP     332, 40, 380, 88
// Hit radius around a shoulder glyph (SVG pixels).
#define SHOULDER_HIT        40

// Analog stick geometry (SVG pixels). The base circle is 62; the usable
// radius maps |offset| == STICK_RADIUS to a full-scale axis value.
#define STICK_RADIUS        50
// Radial dead-zone (~8% of the radius).
#define STICK_DEAD           4

#define ALPHA_OPAQUE       255
#define ZOMBIE_TIMEOUT     500 // ms

// ---------------------------------------------------------------------------
// Home (resting) positions of the controls, in percent of the window. The
// layout mirrors the iOS GCVirtualController in landscape: directional input
// bottom-left, A/B/X/Y bottom-right, the right thumbstick centred between them
// (full layout) and the shoulders in the top corners. Tune these after device
// testing.
// ---------------------------------------------------------------------------
#define DIR_HOME_X_PCT       16
#define DIR_HOME_Y_PCT       72
#define RSTICK_HOME_X_PCT    50
#define RSTICK_HOME_Y_PCT    72
#define ABXY_HOME_X_PCT      84
#define ABXY_HOME_Y_PCT      72
#define SHOULDER_L_X_PCT      7
#define SHOULDER_R_X_PCT     93
#define SHOULDER_Y_PCT       14

// Horizontal regions used to assign a finger to a control when it is not on a
// button/shoulder hotspot. Left region -> directional, right region -> A/B/X/Y,
// the gap in between -> right thumbstick (full layout only).
#define LEFT_REGION_MAX_PCT  40
#define RIGHT_REGION_MIN_PCT 60

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
	_minimalLayout(false),
	_directionalThumbstick(true),
	_baseAlpha(ALPHA_OPAQUE * 6 / 10) {
	_gesturesInsets[0] = _gesturesInsets[1] = _gesturesInsets[2] = _gesturesInsets[3] = 0;
	_functions[kFunctionDirectional] = new FunctionDirectional(this);
	_functions[kFunctionButtons]     = new FunctionButtons(this);
	_functions[kFunctionRightStick]  = new FunctionRightStick(this);
	_functions[kFunctionShoulderL]   = new FunctionShoulder(this, Common::JOYSTICK_BUTTON_LEFT_SHOULDER, false);
	_functions[kFunctionShoulderR]   = new FunctionShoulder(this, Common::JOYSTICK_BUTTON_RIGHT_SHOULDER, true);
}

void TouchControls::init(float scale) {
	// Don't re-open (and on Emscripten re-fetch) the asset once it has failed:
	// init() is otherwise retried lazily every frame while uninitialized.
	if (_loadFailed) {
		return;
	}

	// Register the shared gamepad settings (same keys and defaults as the iOS
	// backend) so reads below never hit an unregistered key.
	ConfMan.registerDefault("gamepad_controller_opacity", 6);
	ConfMan.registerDefault("gamepad_controller_directional_input", kDirectionalInputThumbstick);
	ConfMan.registerDefault("gamepad_controller_minimal_layout", false);

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

	delete _svg;
	_svg = new Graphics::SVGBitmap(&stream, SVG_PIXELS(SVG_WIDTH), SVG_PIXELS(SVG_HEIGHT));
}

TouchControls::~TouchControls() {
	delete _svg;
	for(unsigned int i = 0; i < kFunctionCount; i++) {
		delete _functions[i];
	}
}

void TouchControls::refreshConfig() {
	_directionalThumbstick =
		(ConfMan.getInt("gamepad_controller_directional_input") == kDirectionalInputThumbstick);
	_minimalLayout = ConfMan.getBool("gamepad_controller_minimal_layout");
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

bool TouchControls::isFunctionPresent(FunctionId id) const {
	switch (id) {
	case kFunctionDirectional:
	case kFunctionButtons:
		return true;
	case kFunctionRightStick:
	case kFunctionShoulderL:
	case kFunctionShoulderR:
		// Full layout only.
		return !_minimalLayout;
	default:
		return false;
	}
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

	// Priority: button/shoulder hotspots first, then the horizontal regions
	// (directional, then right thumbstick, then A/B/X/Y).
	if (!_minimalLayout) {
		if (_functions[kFunctionShoulderL]->isInside(x, y)) {
			return kFunctionShoulderL;
		}
		if (_functions[kFunctionShoulderR]->isInside(x, y)) {
			return kFunctionShoulderR;
		}
	}

	int xRatio = x * 100 / (int)_screen_width;

	if (xRatio < LEFT_REGION_MAX_PCT) {
		return kFunctionDirectional;
	} else if (xRatio >= RIGHT_REGION_MIN_PCT) {
		return kFunctionButtons;
	} else if (!_minimalLayout) {
		return kFunctionRightStick;
	}

	return kFunctionNone;
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
		if (!isFunctionPresent((FunctionId)i)) {
			continue;
		}
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

	uint32 now = g_system->getMillis(true);

	for (uint i = 0; i < kFunctionCount; i++) {
		if (!isFunctionPresent((FunctionId)i)) {
			continue;
		}
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
// Directional input (left thumbstick or D-pad)
// ===========================================================================

bool TouchControls::FunctionDirectional::isInside(int x, int y) {
	int dX = x - startX;
	int dY = y - startY;
	// norm 2 squared (to avoid square root)
	unsigned int sqNorm = (unsigned int)(dX * dX) + (unsigned int)(dY * dY);

	if (sqNorm <= FUNC_SVG_SQ_SCALED(BUTTON_END)) {
		return true;
	}

	// Also accept touches near the old last touch
	dX = x - currentX;
	dY = y - currentY;
	sqNorm = (unsigned int)(dX * dX) + (unsigned int)(dY * dY);

	if (sqNorm <= FUNC_SVG_SQ_SCALED(ARROW_DEAD)) {
		return true;
	}

	return false;
}

uint32 TouchControls::FunctionDirectional::directionMask(int dX, int dY) const {
	uint32 newMask = 0;

	// norm 2 squared (to avoid square root)
	unsigned int sqNorm = (unsigned int)(dX * dX) + (unsigned int)(dY * dY);

	if (sqNorm >= FUNC_SVG_SQ_SCALED(ARROW_DEAD)) {
		// We are far enough from the center.
		// Up/down sensitive zone: angles outside +/-30deg (1/sqrt(3)).
		// Left/right sensitive zone: angles within +/-60deg (sqrt(3)).
		unsigned int sq3 = abs(dX) * 51409 / 29681;
		unsigned int isq3 = abs(dX) * 29681 / 51409;
		unsigned int adY = abs(dY);

		if (adY <= sq3) {
			// Left or right
			newMask |= (dX < 0) ? 8 : 2;
		}
		if (adY >= isq3) {
			// Up or down
			newMask |= (dY < 0) ? 1 : 4;
		}
	}

	return newMask;
}

void TouchControls::FunctionDirectional::maskToDpadButtons(uint32 oldMask, uint32 newMask) {
	static const Common::JoystickButton buttons[] = {
		Common::JOYSTICK_BUTTON_DPAD_UP, Common::JOYSTICK_BUTTON_DPAD_RIGHT,
		Common::JOYSTICK_BUTTON_DPAD_DOWN, Common::JOYSTICK_BUTTON_DPAD_LEFT
	};

	uint32 diff = newMask ^ oldMask;

	for (int i = 0, m = 1; i < ARRAYSIZE(buttons); i++, m <<= 1) {
		if (!(diff & m)) {
			continue;
		}
		if (oldMask & m) {
			TouchControls::buttonUp(buttons[i]);
		}
	}
	for (int i = 0, m = 1; i < ARRAYSIZE(buttons); i++, m <<= 1) {
		if (!(diff & m)) {
			continue;
		}
		if (newMask & m) {
			TouchControls::buttonDown(buttons[i]);
		}
	}
}

void TouchControls::FunctionDirectional::touchDpad(int dX, int dY, Action action) {
	if (action == kActionCancel || action == kActionUp) {
		maskToDpadButtons(mask, 0);
		mask = 0;
		return;
	}

	uint32 newMask = directionMask(dX, dY);
	if (mask != newMask) {
		maskToDpadButtons(mask, newMask);
	}
	mask = newMask;
}

void TouchControls::FunctionDirectional::touchThumb(int dX, int dY, Action action,
		Common::JoystickAxis ax, Common::JoystickAxis ay) {
	int16 newX = 0, newY = 0;

	if (action != kActionCancel && action != kActionUp) {
		unsigned int sqNorm = (unsigned int)(dX * dX) + (unsigned int)(dY * dY);
		if (sqNorm >= FUNC_SVG_SQ_SCALED(STICK_DEAD)) {
			// radiusFP maps |offset| == STICK_RADIUS to a full-scale value.
			// It is expressed in the same fixed-point screen units as dX/dY.
			int radiusFP = SVG_UNSCALED(STICK_RADIUS) * (int)parent->_scale;
			if (radiusFP > 0) {
				// Per-axis clip (square envelope); good enough for a first pass.
				newX = (int16)CLIP(dX * Common::JOYAXIS_MAX / radiusFP,
						(int)Common::JOYAXIS_MIN, (int)Common::JOYAXIS_MAX);
				newY = (int16)CLIP(dY * Common::JOYAXIS_MAX / radiusFP,
						(int)Common::JOYAXIS_MIN, (int)Common::JOYAXIS_MAX);
			}
		}
		// Reuse the D-pad direction logic only to light up the arrows.
		mask = directionMask(dX, dY);
	} else {
		mask = 0;
	}

	if (newX != axisX) {
		TouchControls::axisMotion(ax, newX);
		axisX = newX;
	}
	if (newY != axisY) {
		TouchControls::axisMotion(ay, newY);
		axisY = newY;
	}
}

void TouchControls::FunctionDirectional::touch(int dX, int dY, Action action) {
	if (parent->_directionalThumbstick) {
		touchThumb(dX, dY, action,
				Common::JOYSTICK_AXIS_LEFT_STICK_X, Common::JOYSTICK_AXIS_LEFT_STICK_Y);
	} else {
		touchDpad(dX, dY, action);
	}
}

void TouchControls::FunctionDirectional::drawStick(uint8 baseAlpha, uint8 hlAlpha, int cx, int cy, uint32 dirMask) {
	// Base: D-pad cell (circle + grey arrows), reused for both modes.
	{
		Common::Rect clip(0, 0, BG_WIDTH, BG_HEIGHT);
		parent->drawSurface(baseAlpha, cx, cy, -clip.width() / 2, -clip.height() / 2, clip);
	}

	if (!hlAlpha || !dirMask) {
		return;
	}

	// Pressed arrows from the highlight row.
	if (dirMask & 1) {
		Common::Rect clip(0, ARROW_HL_TOP, ARROW_WIDTH, ARROW_HL_TOP + ARROW_HEIGHT);
		parent->drawSurface(hlAlpha, cx, cy, -1 * clip.width() / 2, -2 * clip.height() / 2, clip);
	}
	if (dirMask & 2) {
		Common::Rect clip(ARROW_WIDTH, ARROW_HL_TOP, 2 * ARROW_WIDTH, ARROW_HL_TOP + ARROW_HEIGHT);
		parent->drawSurface(hlAlpha, cx, cy, 0, -1 * clip.height() / 2, clip);
	}
	if (dirMask & 4) {
		Common::Rect clip(0, ARROW_HL_TOP + ARROW_HEIGHT, ARROW_WIDTH, ARROW_HL_TOP + 2 * ARROW_HEIGHT);
		parent->drawSurface(hlAlpha, cx, cy, -1 * clip.width() / 2, 0, clip);
	}
	if (dirMask & 8) {
		Common::Rect clip(ARROW_WIDTH, ARROW_HL_TOP + ARROW_HEIGHT, 2 * ARROW_WIDTH, ARROW_HL_TOP + 2 * ARROW_HEIGHT);
		parent->drawSurface(hlAlpha, cx, cy, -2 * clip.width() / 2, -1 * clip.height() / 2, clip);
	}
}

void TouchControls::FunctionDirectional::draw(uint8 baseAlpha, uint8 hlAlpha) {
	bool active = (status != kFunctionInactive);
	int cx = active ? startX : parent->homeX(DIR_HOME_X_PCT);
	int cy = active ? startY : parent->homeY(DIR_HOME_Y_PCT);
	drawStick(baseAlpha, hlAlpha, cx, cy, mask);
}

// ===========================================================================
// Right thumbstick (full layout)
// ===========================================================================

bool TouchControls::FunctionRightStick::isInside(int x, int y) {
	int dX = x - startX;
	int dY = y - startY;
	unsigned int sqNorm = (unsigned int)(dX * dX) + (unsigned int)(dY * dY);
	return sqNorm <= FUNC_SVG_SQ_SCALED(BUTTON_END);
}

void TouchControls::FunctionRightStick::touch(int dX, int dY, Action action) {
	int16 newX = 0, newY = 0;

	if (action != kActionCancel && action != kActionUp) {
		unsigned int sqNorm = (unsigned int)(dX * dX) + (unsigned int)(dY * dY);
		if (sqNorm >= FUNC_SVG_SQ_SCALED(STICK_DEAD)) {
			int radiusFP = SVG_UNSCALED(STICK_RADIUS) * (int)parent->_scale;
			if (radiusFP > 0) {
				newX = (int16)CLIP(dX * Common::JOYAXIS_MAX / radiusFP,
						(int)Common::JOYAXIS_MIN, (int)Common::JOYAXIS_MAX);
				newY = (int16)CLIP(dY * Common::JOYAXIS_MAX / radiusFP,
						(int)Common::JOYAXIS_MIN, (int)Common::JOYAXIS_MAX);
			}
			// Light up arrows roughly in the offset direction.
			mask = 0;
			if (abs(dX) > abs(dY) / 2) {
				mask |= (dX < 0) ? 8 : 2;
			}
			if (abs(dY) > abs(dX) / 2) {
				mask |= (dY < 0) ? 1 : 4;
			}
		} else {
			mask = 0;
		}
	} else {
		mask = 0;
	}

	if (newX != axisX) {
		TouchControls::axisMotion(Common::JOYSTICK_AXIS_RIGHT_STICK_X, newX);
		axisX = newX;
	}
	if (newY != axisY) {
		TouchControls::axisMotion(Common::JOYSTICK_AXIS_RIGHT_STICK_Y, newY);
		axisY = newY;
	}
}

void TouchControls::FunctionRightStick::draw(uint8 baseAlpha, uint8 hlAlpha) {
	bool active = (status != kFunctionInactive);
	int cx = active ? startX : parent->homeX(RSTICK_HOME_X_PCT);
	int cy = active ? startY : parent->homeY(RSTICK_HOME_Y_PCT);

	// Reuse the directional stick glyph (D-pad cell + arrows).
	FunctionDirectional *dir =
		static_cast<FunctionDirectional *>(parent->_functions[kFunctionDirectional]);
	dir->drawStick(baseAlpha, hlAlpha, cx, cy, mask);
}

// ===========================================================================
// A/B/X/Y face buttons (right cluster)
// ===========================================================================

bool TouchControls::FunctionButtons::isInside(int x, int y) {
	int dX = x - startX;
	int dY = y - startY;
	unsigned int sqNorm = (unsigned int)(dX * dX) + (unsigned int)(dY * dY);
	return sqNorm <= FUNC_SVG_SQ_SCALED(BUTTON_END);
}

void TouchControls::FunctionButtons::touch(int dX, int dY, Action action) {
	static const Common::JoystickButton buttons[] = {
		Common::JOYSTICK_BUTTON_Y, Common::JOYSTICK_BUTTON_B,
		Common::JOYSTICK_BUTTON_A, Common::JOYSTICK_BUTTON_X
	};
	if (action == kActionCancel || action == kActionUp) {
		if (button) {
			buttonUp(buttons[button - 1]);
		}
		button = 0;
		return;
	}

	uint32 newButton = 0;

	unsigned int sqNorm = (unsigned int)(dX * dX) + (unsigned int)(dY * dY);

	if (sqNorm >= FUNC_SVG_SQ_SCALED(BUTTON_DEAD) && sqNorm <= FUNC_SVG_SQ_SCALED(BUTTON_END)) {
		// Pick the button by direction (+/-45deg sectors).
		unsigned int adX = abs(dX);
		unsigned int adY = abs(dY);
		if (adY <= adX) {
			newButton = (dX < 0) ? 4 : 2; // X or B
		} else {
			newButton = (dY < 0) ? 1 : 3; // Y or A
		}
	}

	if (button != newButton) {
		if (button) {
			buttonUp(buttons[button - 1]);
		}
		button = newButton;
		if (button) {
			buttonDown(buttons[button - 1]);
		}
	}
}

void TouchControls::FunctionButtons::draw(uint8 baseAlpha, uint8 hlAlpha) {
	bool active = (status != kFunctionInactive);
	int cx = active ? startX : parent->homeX(ABXY_HOME_X_PCT);
	int cy = active ? startY : parent->homeY(ABXY_HOME_Y_PCT);

	// Resting cluster (column 1, row 0).
	{
		Common::Rect clip(BG_WIDTH, 0, 2 * BG_WIDTH, BG_HEIGHT);
		parent->drawSurface(baseAlpha, cx, cy, -clip.width() / 2, -clip.height() / 2, clip);
	}

	if (!hlAlpha || button == 0) {
		return;
	}

	Common::Rect clip(BUTTON_WIDTH, BUTTON_HEIGHT);
	int16 offX, offY;
	if (button == 1) {        // Y
		clip.translate(BUTTON_HL_LEFT, BUTTON_HL_TOP);
		offX = -1; offY = -2;
	} else if (button == 2) { // B
		clip.translate(BUTTON_HL_LEFT + BUTTON_WIDTH, BUTTON_HL_TOP);
		offX = 0; offY = -1;
	} else if (button == 3) { // A
		clip.translate(BUTTON_HL_LEFT, BUTTON_HL_TOP + BUTTON_HEIGHT);
		offX = -1; offY = 0;
	} else if (button == 4) { // X
		clip.translate(BUTTON_HL_LEFT + BUTTON_WIDTH, BUTTON_HL_TOP + BUTTON_HEIGHT);
		offX = -2; offY = -1;
	} else {
		return;
	}
	parent->drawSurface(hlAlpha, cx, cy, offX * clip.width() / 2, offY * clip.height() / 2, clip);
}

// ===========================================================================
// L1 / R1 shoulder buttons (full layout)
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

	Common::Rect clip = _right ? Common::Rect(SHOULDER_R_CLIP) : Common::Rect(SHOULDER_L_CLIP);
	parent->drawSurface(baseAlpha, cx, cy, -clip.width() / 2, -clip.height() / 2, clip);
	if (hlAlpha) {
		// Brighten the glyph while pressed.
		parent->drawSurface(hlAlpha, cx, cy, -clip.width() / 2, -clip.height() / 2, clip);
	}
}
