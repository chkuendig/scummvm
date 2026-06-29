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

// On-screen touch gamepad controls for the SDL backend (used by Emscripten).
//
// This mirrors the iOS GCVirtualController layout (see
// backends/platform/ios7/ios7_gamepad_controller.mm): a configurable
// directional input (analog left thumbstick or D-pad), the A/B/X/Y face
// buttons and - in the full layout - a right thumbstick (camera look) plus
// L1/R1 shoulder buttons. The layout, opacity and directional mode are taken
// from the shared gamepad_controller_* ConfMan settings so the behaviour is
// consistent with the iOS backend.
//
// Synthetic joystick events (buttons and analog axes) are pushed through the
// generic event manager; the gamepad.svg asset is reused for the glyphs and a
// missing asset is non-fatal.

#ifndef BACKENDS_PLATFORM_SDL_TOUCHCONTROLS_H_
#define BACKENDS_PLATFORM_SDL_TOUCHCONTROLS_H_

#include "common/events.h"

namespace Graphics {
class ManagedSurface;
}

class TouchControlsDrawer {
public:
	virtual void touchControlInitSurface(const Graphics::ManagedSurface &surf) = 0;
	virtual void touchControlNotifyChanged() = 0;
	virtual void touchControlDraw(uint8 alpha, int16 x, int16 y, int16 w, int16 h, const Common::Rect &clip) = 0;

protected:
	~TouchControlsDrawer() {}
};

class TouchControls {
public:
	// action type
	enum Action {
		kActionDown = 0,
		kActionMove = 1,
		kActionUp = 2,
		kActionCancel = 3
	};

	// Directional input mode. The integer values intentionally match the iOS
	// DirectionalInput enum (backends/platform/ios7/ios7_common.h) so the
	// shared "gamepad_controller_directional_input" setting selects the same
	// behaviour on both backends.
	enum DirectionalInput {
		kDirectionalInputThumbstick = 0,
		kDirectionalInputDpad = 1
	};

	TouchControls();
	~TouchControls();

	void init(float scale);
	/** True once the gamepad asset has been loaded successfully. */
	bool isInitialized() const { return _svg != nullptr; }
	void setDrawer(TouchControlsDrawer *drawer, int width, int height);
	void beforeDraw();
	void draw();
	void update(Action action, int ptr, int x, int y);

private:
	TouchControlsDrawer *_drawer;

	unsigned int _screen_width, _screen_height;
	unsigned int _scale, _scale2;

	int _gesturesInsets[4];

	Graphics::ManagedSurface *_svg;
	// Latched once the gamepad asset fails to load, so init() does not re-open
	// (and re-fetch, on Emscripten) a missing gamepad.svg on every frame.
	bool _loadFailed;

	unsigned int _zombieCount;

	// Layout / appearance, refreshed every frame from ConfMan (see
	// refreshConfig()). They mirror the iOS GCVirtualController settings.
	bool _minimalLayout;          // true -> directional + A/B/X/Y only
	bool _directionalThumbstick;  // true -> left thumbstick, false -> D-pad
	uint8 _baseAlpha;             // resting opacity (gamepad_controller_opacity)
	void refreshConfig();

	// Home (resting) centre of a control, in fixed-point screen pixels.
	int homeX(int percent) const { return (int)_screen_width * percent / 100; }
	int homeY(int percent) const { return (int)_screen_height * percent / 100; }

	enum State {
		kFunctionInactive = 0,
		kFunctionActive = 1,
		kFunctionZombie = 2
	};

	struct Function {
		virtual bool isInside(int, int) = 0;
		virtual void touch(int, int, Action) = 0;
		// baseAlpha: resting opacity of the glyph; hlAlpha: opacity of the
		// pressed-state highlight (0 when not pressed / fully faded out).
		virtual void draw(uint8 baseAlpha, uint8 hlAlpha) = 0;
		virtual void resetState() {}

		Function(const TouchControls *parent_) :
			parent(parent_), pointerId(-1),
			startX(-1), startY(-1),
			currentX(-1), currentY(-1),
			lastActivable(0), status(kFunctionInactive) {}
		virtual ~Function() {}

		void reset() {
			pointerId = -1;
			startX = startY = currentX = currentY = -1;
			lastActivable = 0;
			status = kFunctionInactive;
			resetState();
		}

		const TouchControls *parent;

		int pointerId;
		uint16 startX, startY;
		uint16 currentX, currentY;
		uint32 lastActivable;
		State status;
	};
	Function *getFunctionFromPointerId(int ptr);
	Function *getZombieFunctionFromPos(int x, int y);

	enum FunctionId {
		kFunctionNone = -1,
		kFunctionDirectional = 0, // left thumbstick or D-pad (always present)
		kFunctionButtons     = 1, // A/B/X/Y face buttons (always present)
		kFunctionRightStick  = 2, // right thumbstick / look (full layout only)
		kFunctionShoulderL   = 3, // L1 shoulder (full layout only)
		kFunctionShoulderR   = 4, // R1 shoulder (full layout only)
		kFunctionStart       = 5, // Start/menu button (always present)
		kFunctionCount       = 6
	};
	FunctionId getFunctionId(int x, int y);
	bool isFunctionPresent(FunctionId id) const;

	Function *_functions[kFunctionCount];

	static void buttonDown(Common::JoystickButton jb);
	static void buttonUp(Common::JoystickButton jb);
	static void axisMotion(Common::JoystickAxis axis, int16 value);

	/**
	 * Draws a part of the joystick surface on the screen
	 *
	 * @param x     The left coordinate in fixed-point screen pixels
	 * @param y     The top coordinate in fixed-point screen pixels
	 * @param offX  The left offset in SVG pixels
	 * @param offY  The top offset in SVG pixels
	 * @param clip  The clipping rectangle in source surface in SVG pixels
	 */
	void drawSurface(uint8 alpha, int x, int y, int offX, int offY, const Common::Rect &clip) const;


	// Functions implementations

	// Directional input: analog left thumbstick (emits LEFT_STICK axes) or a
	// 4-way D-pad (emits JOYSTICK_BUTTON_DPAD_*), selected at runtime by
	// parent->_directionalThumbstick. The glyph (D-pad cell of gamepad.svg) is
	// reused for both modes; in thumbstick mode the arrows light up to give
	// directional feedback while analog axes are emitted.
	struct FunctionDirectional : Function {
		FunctionDirectional(const TouchControls *parent) :
			Function(parent), mask(0), axisX(0), axisY(0) {}
		void resetState() override { mask = 0; axisX = axisY = 0; }

		bool isInside(int, int) override;
		void touch(int, int, Action) override;
		void draw(uint8 baseAlpha, uint8 hlAlpha) override;

		// Drawn glyph helpers (shared with FunctionRightStick via parent).
		void drawStick(uint8 baseAlpha, uint8 hlAlpha, int cx, int cy, uint32 dirMask);

		uint32 mask;     // last D-pad / display direction mask
		int16 axisX, axisY;
	private:
		void touchDpad(int dX, int dY, Action action);
		void touchThumb(int dX, int dY, Action action,
				Common::JoystickAxis ax, Common::JoystickAxis ay);
		void maskToDpadButtons(uint32 oldMask, uint32 newMask);
		uint32 directionMask(int dX, int dY) const;
	};

	// Right thumbstick (full layout): emits RIGHT_STICK axes. Reuses the same
	// stick glyph as the directional control.
	struct FunctionRightStick : Function {
		FunctionRightStick(const TouchControls *parent) :
			Function(parent), mask(0), axisX(0), axisY(0) {}
		void resetState() override { mask = 0; axisX = axisY = 0; }

		bool isInside(int, int) override;
		void touch(int, int, Action) override;
		void draw(uint8 baseAlpha, uint8 hlAlpha) override;

		uint32 mask;
		int16 axisX, axisY;
	};

	// A/B/X/Y face buttons, picked by direction from the cluster centre
	// (unchanged behaviour from the original right-hand cluster).
	struct FunctionButtons : Function {
		FunctionButtons(const TouchControls *parent) :
			Function(parent), button(0) {}
		void resetState() override { button = 0; }

		bool isInside(int, int) override;
		void touch(int, int, Action) override;
		void draw(uint8 baseAlpha, uint8 hlAlpha) override;

		uint32 button;
	};

	// L1 / R1 shoulder buttons (full layout): fixed hotspots in the top
	// corners. The glyph reuses the labelled circles of gamepad.svg.
	struct FunctionShoulder : Function {
		FunctionShoulder(const TouchControls *parent, Common::JoystickButton jb, bool right) :
			Function(parent), _jb(jb), _right(right), pressed(false) {}
		void resetState() override { pressed = false; }

		bool isInside(int, int) override;
		void touch(int, int, Action) override;
		void draw(uint8 baseAlpha, uint8 hlAlpha) override;

		Common::JoystickButton _jb;
		bool _right;
		bool pressed;
	};

	// Start / menu button: a small fixed hotspot at the top centre, present in
	// both the full and minimal layouts. Emits JOYSTICK_BUTTON_START (mapped to
	// Esc / menu / skip by the engine keymaps). Unlike the iOS GCVirtualController
	// - which cannot expose Start - the custom SDL pad draws this itself, giving
	// touch users a one-tap menu/skip button. The glyph reuses a labelled circle
	// of gamepad.svg (the menu "lens" of column 2).
	struct FunctionStart : Function {
		FunctionStart(const TouchControls *parent) :
			Function(parent), pressed(false) {}
		void resetState() override { pressed = false; }

		bool isInside(int, int) override;
		void touch(int, int, Action) override;
		void draw(uint8 baseAlpha, uint8 hlAlpha) override;

		bool pressed;
	};
};

#endif
