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

#ifndef COMMON_TOUCH_MODE_H
#define COMMON_TOUCH_MODE_H

#include "common/str.h"

namespace Common {

/**
 * On-screen touch-mode presets, shared between the GUI Control tab and the
 * SDL and iOS backends. The values match the popup tag order used by the GUI
 * (and by the Android backend, which keeps its own private copy).
 */
enum TouchMode {
	kTouchModeDefault  = -1, ///< No preset; inherit the global/default value.
	kTouchModeTouchpad = 0,  ///< Touchpad-style relative pointer.
	kTouchModeMouse    = 1,  ///< Direct absolute pointer ("direct" on iOS).
	kTouchModeGamepad  = 2   ///< On-screen virtual gamepad.
};

/** Number of concrete (non-default) touch modes; used when cycling modes. */
enum { kTouchModeCount = 3 };

// ConfMan key names for the per-context touch-mode presets.
#define TOUCH_MODE_MENUS_KEY    "touch_mode_menus"
#define TOUCH_MODE_2D_GAMES_KEY "touch_mode_2d_games"
#define TOUCH_MODE_3D_GAMES_KEY "touch_mode_3d_games"
#define ONSCREEN_CONTROL_KEY    "onscreen_control"

/**
 * Parse a ConfMan touch-mode string into a TouchMode. Both "mouse" (SDL/GUI)
 * and "direct" (iOS) map to the direct-pointer mode. Unknown or empty values
 * return @p fallback.
 */
inline TouchMode parseTouchMode(const Common::String &value, TouchMode fallback = kTouchModeDefault) {
	if (value == "touchpad")
		return kTouchModeTouchpad;
	if (value == "mouse" || value == "direct")
		return kTouchModeMouse;
	if (value == "gamepad")
		return kTouchModeGamepad;
	return fallback;
}

/**
 * Convert a TouchMode into its ConfMan string. Returns nullptr for
 * kTouchModeDefault (the caller should remove the key). @p directPointerName
 * selects the spelling used for the direct-pointer mode: "mouse" (SDL/GUI) or
 * "direct" (iOS).
 */
inline const char *touchModeToString(TouchMode mode, const char *directPointerName = "mouse") {
	switch (mode) {
	case kTouchModeTouchpad:
		return "touchpad";
	case kTouchModeMouse:
		return directPointerName;
	case kTouchModeGamepad:
		return "gamepad";
	default:
		return nullptr;
	}
}

} // End of namespace Common

#endif
