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

#include <cxxtest/TestSuite.h>
#include "common/events.h"
#include "hpl1/engine/input/Gamepad.h"
#include "hpl1/engine/input/InputTypes.h"

class Hpl1GamepadTestSuite : public CxxTest::TestSuite {
	Common::Event axis(hpl::eGamepadAction action, int position, bool relative = false, int delta = 0, bool end = false) {
		Common::Event event;
		event.type = Common::EVENT_CUSTOM_ENGINE_ACTION_AXIS;
		event.customType = action;
		event.joystick.position = position;
		event.joystick.hasMouseMotion = relative;
		event.joystick.mouseMotion = delta;
		event.joystick.mouseMotionEnd = end;
		return event;
	}

public:
	void test_native_hold_neutral_and_opposite_half_reset() {
		Common::Array<Common::Event> events;
		hpl::Gamepad gamepad(nullptr);
		events.push_back(axis(hpl::eGamepadAction_LookRight, Common::JOYAXIS_MAX));
		events.push_back(axis(hpl::eGamepadAction_LookLeft, 0));
		gamepad.Update(events);
		TS_ASSERT_DELTA(gamepad.GetLookAxes(3000).x, 1.f, 0.00001f);
		events.clear();
		gamepad.Update(events);
		TS_ASSERT_DELTA(gamepad.GetLookAxes(3000).x, 1.f, 0.00001f);
		events.push_back(axis(hpl::eGamepadAction_LookRight, 0));
		gamepad.Update(events);
		TS_ASSERT_EQUALS(gamepad.GetLookAxes(3000).x, 0.f);
	}

	void test_dead_zone_zero_and_diagonal_clamp() {
		Common::Array<Common::Event> events;
		hpl::Gamepad gamepad(nullptr);
		TS_ASSERT_EQUALS(gamepad.GetLookAxes(0).x, 0.f);
		events.push_back(axis(hpl::eGamepadAction_LookRight, 2500));
		gamepad.Update(events);
		TS_ASSERT_EQUALS(gamepad.GetLookAxes(3000).x, 0.f);
		events.clear();
		events.push_back(axis(hpl::eGamepadAction_LookRight, Common::JOYAXIS_MAX));
		events.push_back(axis(hpl::eGamepadAction_LookDown, Common::JOYAXIS_MAX));
		gamepad.Update(events);
		hpl::cVector2f axes = gamepad.GetLookAxes(3000);
		TS_ASSERT_DELTA(axes.x, 0.70710678f, 0.00001f);
		TS_ASSERT_DELTA(axes.y, 0.70710678f, 0.00001f);
	}

	void test_look_rotation_is_independent_of_frame_rate() {
		const int rates[] = {30, 60, 120};
		for (int fps : rates) {
			Common::Array<Common::Event> events;
			hpl::Gamepad gamepad(nullptr);
			events.push_back(axis(hpl::eGamepadAction_LookRight, Common::JOYAXIS_MAX));
			gamepad.Update(events);
			events.clear();
			float rotation = 0;
			for (int frame = 0; frame < fps; ++frame) {
				gamepad.Update(events);
				rotation += gamepad.GetLookMotion(1.f / fps, 3000).x;
			}
			TS_ASSERT_DELTA(rotation, 2.5f, 0.0001f);
			TS_ASSERT_EQUALS(gamepad.GetLookMotion(0, 3000).x, 0.f);
		}
	}

	void test_release_does_not_erase_motion_in_the_same_frame() {
		Common::Array<Common::Event> events;
		hpl::Gamepad gamepad(nullptr);
		events.push_back(axis(hpl::eGamepadAction_LookRight, 20000, true, 35));
		events.push_back(axis(hpl::eGamepadAction_LookDown, 10000, true, 14, true));
		events.push_back(axis(hpl::eGamepadAction_LookRight, 0, true, 0));
		events.push_back(axis(hpl::eGamepadAction_LookDown, 0, true, 0, true));
		gamepad.Update(events);
		hpl::cVector2f motion = gamepad.GetRelativeMotion(hpl::cVector2f(0, 0));
		TS_ASSERT_EQUALS(motion.x, 35.f);
		TS_ASSERT_EQUALS(motion.y, 14.f);
		TS_ASSERT_EQUALS(gamepad.GetLookMotion(1.f, 3000).x, 0.f);
	}

	void test_batched_relative_moves_preserve_the_last_move() {
		Common::Array<Common::Event> events;
		hpl::Gamepad gamepad(nullptr);
		events.push_back(axis(hpl::eGamepadAction_LookRight, 20000, true, 10));
		events.push_back(axis(hpl::eGamepadAction_LookDown, 10000, true, 5, true));
		events.push_back(axis(hpl::eGamepadAction_LookRight, 20000, true, 20));
		events.push_back(axis(hpl::eGamepadAction_LookUp, 10000, true, 8, true));
		gamepad.Update(events);
		hpl::cVector2f motion = gamepad.GetRelativeMotion(hpl::cVector2f(0, 0));
		TS_ASSERT_EQUALS(motion.x, 20.f);
		TS_ASSERT_EQUALS(motion.y, -8.f);
		TS_ASSERT_EQUALS(gamepad.GetRelativeMotion(hpl::cVector2f(0, 0)).x, 0.f);
	}

	void test_later_mouse_event_supersedes_relative_stick_motion() {
		Common::Array<Common::Event> events;
		hpl::Gamepad gamepad(nullptr);
		events.push_back(axis(hpl::eGamepadAction_LookRight, 20000, true, 35, true));
		Common::Event mouse;
		mouse.type = Common::EVENT_LBUTTONUP;
		events.push_back(mouse);
		gamepad.Update(events);
		TS_ASSERT_EQUALS(gamepad.GetRelativeMotion(hpl::cVector2f(0, 0)).x, 0.f);
		events.clear();
		events.push_back(mouse);
		events.push_back(axis(hpl::eGamepadAction_LookRight, 20000, true, 35, true));
		gamepad.Update(events);
		TS_ASSERT_EQUALS(gamepad.GetRelativeMotion(hpl::cVector2f(7, 0)).x, 35.f);
	}

	void test_partial_relative_mapping_keeps_the_mapped_axis() {
		Common::Array<Common::Event> events;
		hpl::Gamepad gamepad(nullptr);
		events.push_back(axis(hpl::eGamepadAction_LookRight, 20000, true, 11));
		gamepad.Update(events);
		hpl::cVector2f motion = gamepad.GetRelativeMotion(hpl::cVector2f(0, 0));
		TS_ASSERT_EQUALS(motion.x, 11.f);
		TS_ASSERT_EQUALS(motion.y, 0.f);
	}

	void test_focus_and_input_changes_clear_held_axes() {
		const Common::EventType types[] = {Common::EVENT_FOCUS_LOST, Common::EVENT_INPUT_CHANGED};
		for (Common::EventType type : types) {
			Common::Array<Common::Event> events;
			hpl::Gamepad gamepad(nullptr);
			events.push_back(axis(hpl::eGamepadAction_LookRight, Common::JOYAXIS_MAX));
			gamepad.Update(events);
			events.clear();
			Common::Event event;
			event.type = type;
			events.push_back(event);
			gamepad.Update(events);
			TS_ASSERT_EQUALS(gamepad.GetLookAxes(3000).x, 0.f);
		}
	}
};
