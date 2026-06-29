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
// This implements the standard AAA touch-FPS layout in landscape:
//   - a FLOATING left thumbstick (move) in the bottom-left, recentred under the
//     finger and rendered as a translucent base ring + a filled knob; it emits
//     the LEFT_STICK analog axes;
//   - DRAG-TO-LOOK on the right half of the screen (the camera): an invisible
//     region whose finger drag is turned into relative-mouse motion (mirrors the
//     existing touchpad relmouse scaling), and a short tap there is turned into a
//     left click;
//   - a MULTI-TOUCH A/B/X/Y diamond in the bottom-right (each button its own
//     independent hotspot so several can be held at once);
//   - L1/R1 shoulders in the top corners (labelled pills) and a small Start
//     button at the top centre.
//
// Synthetic joystick / mouse events are pushed through the generic event
// manager; the gamepad.svg asset is reused for the face-button and Start glyphs
// (a missing asset is non-fatal), while the thumbstick ring/knob and the L1/R1
// pills are drawn from primitives into spare regions of the same surface.

#ifndef BACKENDS_PLATFORM_SDL_TOUCHCONTROLS_H_
#define BACKENDS_PLATFORM_SDL_TOUCHCONTROLS_H_

#include "common/events.h"
#include "common/rect.h"

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

	// Resting opacity of the glyphs (gamepad_controller_opacity), refreshed
	// every frame from ConfMan (see refreshConfig()).
	uint8 _baseAlpha;
	void refreshConfig();

	// Draws the thumbstick ring/knob and the L1/R1 pills from primitives into
	// the spare bottom row of the gamepad surface (called once after load).
	void renderPrimitives();

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
		kFunctionNone      = -1,
		kFunctionThumb     = 0, // floating left thumbstick (LEFT_STICK axes)
		kFunctionButtonY   = 1, // A/B/X/Y diamond (independent hotspots)
		kFunctionButtonB   = 2,
		kFunctionButtonA   = 3,
		kFunctionButtonX   = 4,
		kFunctionShoulderL = 5, // L1 shoulder (top-left)
		kFunctionShoulderR = 6, // R1 shoulder (top-right)
		kFunctionStart     = 7, // Start/menu button (top centre)
		kFunctionLook      = 8, // drag-to-look (right half, invisible)
		kFunctionCount     = 9
	};
	FunctionId getFunctionId(int x, int y);

	Function *_functions[kFunctionCount];

	static void buttonDown(Common::JoystickButton jb);
	static void buttonUp(Common::JoystickButton jb);
	static void axisMotion(Common::JoystickAxis axis, int16 value);
	// Relative-mouse motion for drag-to-look (relX/relY in window pixels).
	static void mouseRelative(int relX, int relY);
	// Synthetic left click at the current cursor position (tap-to-click).
	static void mouseClick();

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

	// Floating left thumbstick: recentres under the finger and emits the
	// LEFT_STICK analog axes. Rendered as a translucent base ring + a filled
	// knob that follows the finger (clamped to the stick radius).
	struct FunctionThumb : Function {
		FunctionThumb(const TouchControls *parent) :
			Function(parent), axisX(0), axisY(0), knobX(0), knobY(0) {}
		void resetState() override { axisX = axisY = 0; knobX = knobY = 0; }

		bool isInside(int, int) override;
		void touch(int, int, Action) override;
		void draw(uint8 baseAlpha, uint8 hlAlpha) override;

		int16 axisX, axisY; // last emitted axis values
		int knobX, knobY;   // knob offset from centre (fixed-point px, clamped)
	};

	// One face button (Y/B/A/X) of the bottom-right diamond. Each button is its
	// own independent hotspot so several can be held simultaneously.
	struct FunctionButton : Function {
		FunctionButton(const TouchControls *parent, Common::JoystickButton jb,
				int dxSvg, int dySvg, const Common::Rect &rest, const Common::Rect &hl) :
			Function(parent), _jb(jb), _dxSvg(dxSvg), _dySvg(dySvg),
			_restClip(rest), _hlClip(hl), pressed(false) {}
		void resetState() override { pressed = false; }

		// Centre of this button in fixed-point screen pixels.
		void center(int &cx, int &cy) const;

		bool isInside(int, int) override;
		void touch(int, int, Action) override;
		void draw(uint8 baseAlpha, uint8 hlAlpha) override;

		Common::JoystickButton _jb;
		int _dxSvg, _dySvg;          // diamond offset from cluster centre (SVG px)
		Common::Rect _restClip, _hlClip;
		bool pressed;
	};

	// L1 / R1 shoulder buttons: fixed hotspots in the top corners, drawn as
	// labelled pills.
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

	// Start / menu button: a small fixed hotspot at the top centre. Emits
	// JOYSTICK_BUTTON_START (mapped to Esc / menu / skip by the engine keymaps).
	struct FunctionStart : Function {
		FunctionStart(const TouchControls *parent) :
			Function(parent), pressed(false) {}
		void resetState() override { pressed = false; }

		bool isInside(int, int) override;
		void touch(int, int, Action) override;
		void draw(uint8 baseAlpha, uint8 hlAlpha) override;

		bool pressed;
	};

	// Drag-to-look: the right half of the screen, excluding the button/shoulder
	// hotspots. Invisible (no widget). A drag becomes relative-mouse motion (the
	// camera); a short tap becomes a left click.
	struct FunctionLook : Function {
		FunctionLook(const TouchControls *parent) :
			Function(parent), prevX(0), prevY(0), hiresX(0), hiresY(0), maxTravel(0) {}
		void resetState() override { prevX = prevY = 0; hiresX = hiresY = 0; maxTravel = 0; }

		bool isInside(int, int) override { return false; } // no widget, no zombie reuse
		void touch(int, int, Action) override;
		void draw(uint8, uint8) override {}

		int prevX, prevY;     // previous finger position (fixed-point px)
		float hiresX, hiresY; // sub-pixel relmouse accumulator
		int maxTravel;        // max travel from start (window px), for tap detection
	};
};

#endif
