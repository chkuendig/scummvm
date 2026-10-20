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

#ifndef HPL_GAMEPAD_H
#define HPL_GAMEPAD_H

#include "common/events.h"
#include "hpl1/engine/input/InputDevice.h"
#include "hpl1/engine/math/MathTypes.h"

namespace hpl {

class LowLevelInput;

class Gamepad : public iInputDevice {
public:
	explicit Gamepad(LowLevelInput *input);
	void Update() override;
	void Update(const Common::Array<Common::Event> &events);
	void Reset();
	cVector2f GetLookAxes(int deadZone) const;
	cVector2f GetLookMotion(float timeStep, int deadZone) const;
	cVector2f GetRelativeMotion(const cVector2f &mouseMotion);

private:
	void commitMotion();
	LowLevelInput *_input;
	Common::Point _axisPosition;
	Common::Point _pendingMotion;
	Common::Point _relativeMotion;
	bool _hasPendingMotion;
	bool _relativeOverride;
};

} // namespace hpl

#endif
