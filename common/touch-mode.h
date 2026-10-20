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
 * SDL, Android and iOS backends. The values also cross the Android JNI bridge.
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

enum TouchContext {
	kTouchContextMenus,
	kTouchContext2d,
	kTouchContext3d
};

inline TouchContext touchContext(bool rendering3d, bool overlayInGUI) {
	return overlayInGUI ? kTouchContextMenus : (rendering3d ? kTouchContext3d : kTouchContext2d);
}

inline const char *touchModeKeyForContext(TouchContext context) {
	switch (context) {
	case kTouchContextMenus:
		return TOUCH_MODE_MENUS_KEY;
	case kTouchContext3d:
		return TOUCH_MODE_3D_GAMES_KEY;
	default:
		return TOUCH_MODE_2D_GAMES_KEY;
	}
}

inline TouchMode defaultTouchMode(TouchContext context) {
	return context == kTouchContextMenus ? kTouchModeMouse :
	       (context == kTouchContext3d ? kTouchModeGamepad : kTouchModeTouchpad);
}

/** Keep the requested preset intact while hardware or UI availability changes. */
inline TouchMode resolveTouchMode(TouchMode requested, TouchContext context, bool gamepadAvailable) {
	if (requested == kTouchModeDefault)
		requested = defaultTouchMode(context);
	if (requested == kTouchModeGamepad) {
		if (context == kTouchContextMenus)
			return kTouchModeMouse;
		if (!gamepadAvailable)
			return kTouchModeTouchpad;
	}
	return requested;
}

inline TouchMode nextTouchMode(TouchMode current, TouchContext context, bool gamepadAvailable) {
	TouchMode next = (TouchMode)((current + 1) % kTouchModeCount);
	if (next == kTouchModeGamepad && (context == kTouchContextMenus || !gamepadAvailable))
		next = (TouchMode)((next + 1) % kTouchModeCount);
	return next;
}

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
