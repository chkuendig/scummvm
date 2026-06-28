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
//   - a visible D-PAD (move) in the bottom-left, recentred under the finger and
//     rendered as the gamepad.svg D-pad cell glyph; it emits the digital
//     JOYSTICK_BUTTON_DPAD_UP/DOWN/LEFT/RIGHT buttons (8-way, diagonals fire two);
//   - DRAG-TO-LOOK on the right half of the screen (the camera): an invisible
//     region whose finger drag is turned into relative-mouse motion (mirrors the
//     existing touchpad relmouse scaling), and a short tap there is turned into a
//     left click;
//   - a MULTI-TOUCH A/B/X/Y diamond in the bottom-right (each button its own
//     independent hotspot so several can be held at once);
//   - a compact secondary top-centre cluster reusing the gamepad.svg "system"
//     cluster art (a diamond on a grouping circle): the GMM button (opens the
//     Global Main Menu) on top, R1 on the right, Start at the bottom, L1 on left.
//
// The whole pad is hidden unless the on-screen gamepad is the active touch mode.
//
// Synthetic joystick / mouse events are pushed through the generic event
// manager; the gamepad.svg asset is reused for the face-button and Start glyphs
// (a missing asset is non-fatal), while the thumbstick ring/knob and the L1/R1
// pills are drawn from primitives into spare regions of the same surface.

#ifndef BACKENDS_PLATFORM_SDL_TOUCHCONTROLS_H_
#define BACKENDS_PLATFORM_SDL_TOUCHCONTROLS_H_

#include "backends/platform/sdl/touch-action.h"

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
	// Finger-action type (see backends/platform/sdl/touch-action.h).
	typedef TouchAction Action;

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

	// Centre of a top-cluster button placed at the given SVG-px diamond offset from
	// the cluster centre, in fixed-point screen pixels (offset scaled by the
	// cluster size so the hotspots sit on the drawn buttons, like the A/B/X/Y one).
	void auxCenter(int dxSvg, int dySvg, int &cx, int &cy) const;

	// Overlays one top-cluster pressed-highlight crop (col-2 highlight row) on the
	// cluster centre, offset by half-cells like the A/B/X/Y highlights.
	void drawSysHighlight(uint8 hlAlpha, const Common::Rect &clip, int hlMulX, int hlMulY) const;

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
		kFunctionDpad      = 0, // visible D-pad (DPAD_UP/DOWN/LEFT/RIGHT buttons)
		kFunctionButtonY   = 1, // A/B/X/Y diamond (independent hotspots)
		kFunctionButtonB   = 2,
		kFunctionButtonA   = 3,
		kFunctionButtonX   = 4,
		kFunctionShoulderL = 5, // L1 shoulder (top-left)
		kFunctionShoulderR = 6, // R1 shoulder (top-right)
		kFunctionStart     = 7, // Start/menu button (top centre)
		kFunctionScummVM   = 8, // ScummVM "S" button (opens the Global Main Menu)
		kFunctionLook      = 9, // drag-to-look (right half, invisible)
		kFunctionCount     = 10
	};
	FunctionId getFunctionId(int x, int y);

	Function *_functions[kFunctionCount];

	static void buttonDown(Common::JoystickButton jb);
	static void buttonUp(Common::JoystickButton jb);
	// Synthetic keyboard key press/release, used to drive enumerable GUIs (the
	// launcher and the GMM) keyboard-style from the on-screen pad.
	static void keyDown(Common::KeyCode kc);
	static void keyUp(Common::KeyCode kc);
	static void axisMotion(Common::JoystickAxis axis, int16 value);
	// Relative-mouse motion for drag-to-look (relX/relY in window pixels).
	static void mouseRelative(int relX, int relY);
	// Synthetic left click at the current cursor position (tap-to-click).
	static void mouseClick();
	// Opens the ScummVM Global Main Menu (pushes EVENT_MAINMENU).
	static void mainMenu();

	/**
	 * Draws a part of the joystick surface on the screen
	 *
	 * @param x     The left coordinate in fixed-point screen pixels
	 * @param y     The top coordinate in fixed-point screen pixels
	 * @param offX  The left offset in SVG pixels
	 * @param offY  The top offset in SVG pixels
	 * @param clip  The clipping rectangle in source surface in SVG pixels
	 * @param sizePct Glyph size as a percentage of its native size (100 = 1:1);
	 *               the SVG-px offsets are scaled identically so centring holds.
	 */
	void drawSurface(uint8 alpha, int x, int y, int offX, int offY, const Common::Rect &clip, int sizePct = 100) const;


	// Functions implementations

	// Floating D-pad: recentres under the finger and emits the digital
	// JOYSTICK_BUTTON_DPAD_UP/DOWN/LEFT/RIGHT buttons (8-way; diagonals fire two
	// buttons). Rendered as the gamepad.svg D-pad cell glyph with the pressed
	// arrows lit while held.
	struct FunctionDpad : Function {
		FunctionDpad(const TouchControls *parent) :
			Function(parent), mask(0) {}
		void resetState() override { mask = 0; }

		bool isInside(int, int) override;
		void touch(int, int, Action) override;
		void draw(uint8 baseAlpha, uint8 hlAlpha) override;

		// Maps a finger offset to an octant direction mask
		// (bit 0 up, 1 right, 2 down, 3 left).
		uint32 directionMask(int dX, int dY) const;
		// Emits the joystick button up/down transitions between two masks.
		static void maskToDpadButtons(uint32 oldMask, uint32 newMask);

		uint32 mask; // currently pressed directions (octant bitmask)
	};

	// GMM button: the top of the system-cluster diamond (GUIDE glyph). Opens the
	// Global Main Menu (GMM) on press.
	struct FunctionScummVM : Function {
		FunctionScummVM(const TouchControls *parent) :
			Function(parent), pressed(false) {}
		void resetState() override { pressed = false; }

		bool isInside(int, int) override;
		void touch(int, int, Action) override;
		void draw(uint8 baseAlpha, uint8 hlAlpha) override;

		bool pressed;
	};

	// One face button (Y/B/A/X) of the bottom-right diamond. Each button is its
	// own independent hotspot so several can be held simultaneously. The whole
	// resting cluster glyph is drawn once (by the cluster owner) and each button
	// draws its own pressed highlight, so the glyphs stay clean and uncut.
	struct FunctionButton : Function {
		FunctionButton(const TouchControls *parent, Common::JoystickButton jb,
				int dxSvg, int dySvg, const Common::Rect &hl,
				int hlMulX, int hlMulY, bool clusterOwner) :
			Function(parent), _jb(jb), _dxSvg(dxSvg), _dySvg(dySvg),
			_hlClip(hl), _hlMulX(hlMulX), _hlMulY(hlMulY),
			_clusterOwner(clusterOwner), pressed(false), _menuActivate(false) {}
		void resetState() override { pressed = false; _menuActivate = false; }

		// Centre of this button in fixed-point screen pixels.
		void center(int &cx, int &cy) const;

		bool isInside(int, int) override;
		void touch(int, int, Action) override;
		void draw(uint8 baseAlpha, uint8 hlAlpha) override;

		Common::JoystickButton _jb;
		int _dxSvg, _dySvg;          // letter offset from cluster centre (SVG px)
		Common::Rect _hlClip;        // pressed-highlight crop (col 1 row 1)
		int _hlMulX, _hlMulY;        // highlight offset from cluster centre (half-cells)
		bool _clusterOwner;          // draws the shared resting cluster glyph
		bool pressed;
		// True while this press is acting as a GUI "activate" (Enter) instead of a
		// gamepad A, decided at press time so the matching release matches it.
		bool _menuActivate;
	};

	// L1 / R1 shoulder buttons: the left (LS) and right (RS) points of the
	// system-cluster diamond.
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

	// Start button: the bottom (silver lens) of the system-cluster diamond. Emits
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
		// Draws the ring/dot drag indicator while actively dragging.
		void draw(uint8 baseAlpha, uint8 hlAlpha) override;

		int prevX, prevY;     // previous finger position (fixed-point px)
		float hiresX, hiresY; // sub-pixel relmouse accumulator
		int maxTravel;        // max travel from start (window px), for tap detection
	};
};

#endif
