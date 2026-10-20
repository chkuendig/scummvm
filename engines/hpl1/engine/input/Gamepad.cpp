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

#include "hpl1/engine/input/Gamepad.h"
#include "hpl1/engine/input/LowLevelInput.h"

#include "common/util.h"

namespace hpl {

Gamepad::Gamepad(LowLevelInput *input) :
		iInputDevice("Gamepad", eInputDeviceType_Gamepad), _input(input),
		_hasPendingMotion(false), _relativeOverride(false) {
}

void Gamepad::Reset() {
	_axisPosition = Common::Point();
	_pendingMotion = Common::Point();
	_relativeMotion = Common::Point();
	_hasPendingMotion = false;
	_relativeOverride = false;
}

void Gamepad::commitMotion() {
	// Preserve the last mouse-equivalent motion, including input batching.
	if (_hasPendingMotion && _pendingMotion != Common::Point()) {
		_relativeMotion = _pendingMotion;
		_relativeOverride = true;
	}
	_pendingMotion = Common::Point();
	_hasPendingMotion = false;
}

void Gamepad::Update() {
	Update(_input->_events);
}

void Gamepad::Update(const Common::Array<Common::Event> &events) {
	_pendingMotion = Common::Point();
	_relativeMotion = Common::Point();
	_hasPendingMotion = false;
	_relativeOverride = false;

	for (const auto &event : events) {
		if (event.type == Common::EVENT_INPUT_CHANGED || event.type == Common::EVENT_FOCUS_LOST) {
			Reset();
			continue;
		}
		if (Common::isMouseEvent(event)) {
			// A later mouse/button event supersedes earlier relative input.
			_pendingMotion = Common::Point();
			_hasPendingMotion = false;
			_relativeOverride = false;
			continue;
		}
		if (event.type != Common::EVENT_CUSTOM_ENGINE_ACTION_AXIS) {
			continue;
		}
		bool vertical;
		int direction;
		switch (event.customType) {
		case eGamepadAction_LookUp:    vertical = true;  direction = -1; break;
		case eGamepadAction_LookDown:  vertical = true;  direction =  1; break;
		case eGamepadAction_LookLeft:  vertical = false; direction = -1; break;
		case eGamepadAction_LookRight: vertical = false; direction =  1; break;
		default:
			continue;
		}
		int16 &axis = vertical ? _axisPosition.y : _axisPosition.x;
		if (event.joystick.hasMouseMotion) {
			axis = 0;
			int16 &delta = vertical ? _pendingMotion.y : _pendingMotion.x;
			delta = direction * event.joystick.mouseMotion;
			_hasPendingMotion = true;
			if (event.joystick.mouseMotionEnd) {
				commitMotion();
			}
		} else {
			int position = CLIP<int>(event.joystick.position, 0, Common::JOYAXIS_MAX);
			if (position != 0 || axis * direction >= 0) {
				axis = direction * position;
			}
		}
	}
	commitMotion();
}

cVector2f Gamepad::GetLookAxes(int deadZone) const {
	float x = _axisPosition.x / (float)Common::JOYAXIS_MAX;
	float y = _axisPosition.y / (float)Common::JOYAXIS_MAX;
	float magnitude = sqrtf(x * x + y * y);
	float dead = CLIP<int>(deadZone, 0, Common::JOYAXIS_MAX - 1) / (float)Common::JOYAXIS_MAX;
	if (magnitude <= dead) {
		return cVector2f(0, 0);
	}
	float factor = MIN(1.f, (magnitude - dead) / (1.f - dead)) / magnitude;
	return cVector2f(x * factor, y * factor);
}

cVector2f Gamepad::GetLookMotion(float timeStep, int deadZone) const {
	const float lookRadiansPerSecond = 2.5f;
	return GetLookAxes(deadZone) * (lookRadiansPerSecond * MAX(0.f, timeStep));
}

cVector2f Gamepad::GetRelativeMotion(const cVector2f &mouseMotion) {
	cVector2f motion = _relativeOverride ? cVector2f(_relativeMotion.x, _relativeMotion.y) : mouseMotion;
	_relativeOverride = false;
	_relativeMotion = Common::Point();
	return motion;
}

} // namespace hpl
