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
#include "backends/keymapper/action.h"
#include "backends/keymapper/keymapper.h"
#include "backends/keymapper/virtual-mouse.h"

class KeymapperMouseMotionTestSuite : public CxxTest::TestSuite {
	class Events : public Common::EventManager {
	public:
		bool pollEvent(Common::Event &) override { return false; }
		void pushEvent(const Common::Event &) override {}
		void purgeMouseEvents() override {}
		void purgeKeyboardEvents() override {}
		Common::Point getMousePos() const override { return Common::Point(320, 240); }
		int getButtonState() const override { return 0; }
		int getModifierState() const override { return 0; }
		int shouldQuit() const override { return 0; }
		int shouldReturnToLauncher() const override { return 0; }
		void resetReturnToLauncher() override {}
		void resetQuit() override {}
		Common::Keymapper *getKeymapper() override { return nullptr; }
		Common::Keymap *getGlobalKeymap() override { return nullptr; }
	};

	void bind(Common::Keymap *keymap, const char *id, uint32 type, byte axis, bool positive) {
		Common::Action *action = new Common::Action(id, Common::U32String());
		action->setCustomBackendActionAxisEvent(type);
		keymap->addAction(action);
		keymap->registerMapping(action, Common::HardwareInput::createJoystickHalfAxis("", axis, positive, Common::U32String()));
	}

	void bindEngine(Common::Keymap *keymap, const char *id, uint32 type, byte axis, bool positive) {
		Common::Action *action = new Common::Action(id, Common::U32String());
		action->setCustomEngineActionAxisEvent(type);
		keymap->addAction(action);
		keymap->registerMapping(action, Common::HardwareInput::createJoystickHalfAxis("", axis, positive, Common::U32String()));
	}

	Common::Keymap *keymap(Common::Keymapper &mapper) {
		mapper.registerHardwareInputSet(new Common::JoystickHardwareInputSet(Common::defaultJoystickButtons, Common::defaultJoystickAxes), nullptr);
		Common::Keymap *map = new Common::Keymap(Common::Keymap::kKeymapTypeGlobal, "motion-test", Common::U32String());
		mapper.addGlobalKeymap(map);
		return map;
	}

	Common::Event axis(byte axis, int position, int delta, bool end) {
		Common::Event event;
		event.type = Common::EVENT_JOYAXIS_MOTION;
		event.joystick.axis = axis;
		event.joystick.position = position;
		event.joystick.hasMouseMotion = true;
		event.joystick.mouseMotion = delta;
		event.joystick.mouseMotionEnd = end;
		return event;
	}

public:
	void test_combines_axes_without_changing_mouse_position() {
		Events events;
		Common::Keymapper mapper(&events);
		Common::Keymap *map = keymap(mapper);
		bind(map, "right", Common::VirtualMouse::kCustomActionVirtualAxisRight, Common::JOYSTICK_AXIS_RIGHT_STICK_X, true);
		bind(map, "up", Common::VirtualMouse::kCustomActionVirtualAxisUp, Common::JOYSTICK_AXIS_RIGHT_STICK_Y, false);
		Common::List<Common::Event> result;
		TS_ASSERT(mapper.mapEvent(axis(Common::JOYSTICK_AXIS_RIGHT_STICK_X, 32767, 120, false), result));
		TS_ASSERT(result.empty());
		TS_ASSERT(mapper.mapEvent(axis(Common::JOYSTICK_AXIS_RIGHT_STICK_Y, -32767, -80, true), result));
		TS_ASSERT_EQUALS(result.size(), 1U);
		TS_ASSERT_EQUALS(result.front().type, Common::EVENT_MOUSEMOVE);
		TS_ASSERT_EQUALS(result.front().mouse, Common::Point(320, 240));
		TS_ASSERT_EQUALS(result.front().relMouse, Common::Point(120, -80));
	}

	void test_reversal_and_centre_do_not_duplicate_motion() {
		Events events;
		Common::Keymapper mapper(&events);
		Common::Keymap *map = keymap(mapper);
		bind(map, "right", Common::VirtualMouse::kCustomActionVirtualAxisRight, Common::JOYSTICK_AXIS_RIGHT_STICK_X, true);
		bind(map, "left", Common::VirtualMouse::kCustomActionVirtualAxisLeft, Common::JOYSTICK_AXIS_RIGHT_STICK_X, false);
		const int positions[] = {10000, 0, -10000};
		for (int position : positions) {
			Common::List<Common::Event> result;
			mapper.mapEvent(axis(Common::JOYSTICK_AXIS_RIGHT_STICK_X, position, -37, true), result);
			TS_ASSERT_EQUALS(result.size(), 1U);
			TS_ASSERT_EQUALS(result.front().relMouse, Common::Point(-37, 0));
		}
		Common::List<Common::Event> result;
		mapper.mapEvent(axis(Common::JOYSTICK_AXIS_RIGHT_STICK_X, 0, 0, true), result);
		TS_ASSERT(result.empty());
	}

	void test_remapped_and_unmapped_axes() {
		Events events;
		Common::Keymapper mapper(&events);
		Common::Keymap *map = keymap(mapper);
		bind(map, "up", Common::VirtualMouse::kCustomActionVirtualAxisUp, Common::JOYSTICK_AXIS_RIGHT_STICK_X, true);
		Common::List<Common::Event> result;
		mapper.mapEvent(axis(Common::JOYSTICK_AXIS_RIGHT_STICK_X, 2000, 11, false), result);
		TS_ASSERT(mapper.mapEvent(axis(Common::JOYSTICK_AXIS_RIGHT_STICK_Y, 4000, 19, true), result));
		TS_ASSERT_EQUALS(result.size(), 2U);
		TS_ASSERT_EQUALS(result.front().type, Common::EVENT_JOYAXIS_MOTION);
		TS_ASSERT_EQUALS(result.back().relMouse, Common::Point(0, -11));
	}

	void test_unmapped_axes_remain_available_to_native_consumers() {
		Events events;
		Common::Keymapper mapper(&events);
		keymap(mapper);
		Common::Event event = axis(Common::JOYSTICK_AXIS_RIGHT_STICK_X, -24000, -120, true);
		Common::List<Common::Event> result;
		TS_ASSERT(!mapper.mapEvent(event, result));
		TS_ASSERT(result.empty());
		TS_ASSERT_EQUALS(event.joystick.position, -24000);
	}

	void test_native_engine_axis_retains_motion_when_reversing() {
		Events events;
		Common::Keymapper mapper(&events);
		Common::Keymap *map = keymap(mapper);
		bindEngine(map, "look", 42, Common::JOYSTICK_AXIS_RIGHT_STICK_X, false);
		Common::List<Common::Event> result;
		mapper.mapEvent(axis(Common::JOYSTICK_AXIS_RIGHT_STICK_X, -16000, 7, true), result);
		TS_ASSERT_EQUALS(result.size(), 1U);
		TS_ASSERT_EQUALS(result.front().type, Common::EVENT_CUSTOM_ENGINE_ACTION_AXIS);
		TS_ASSERT_EQUALS(result.front().customType, 42U);
		TS_ASSERT_EQUALS(result.front().joystick.position, 16000);
		TS_ASSERT(result.front().joystick.hasMouseMotion);
		TS_ASSERT_EQUALS(result.front().joystick.mouseMotion, -7);
	}

	void test_native_engine_centre_has_only_one_relative_half() {
		Events events;
		Common::Keymapper mapper(&events);
		Common::Keymap *map = keymap(mapper);
		bindEngine(map, "right", 42, Common::JOYSTICK_AXIS_RIGHT_STICK_X, true);
		bindEngine(map, "left", 43, Common::JOYSTICK_AXIS_RIGHT_STICK_X, false);
		Common::List<Common::Event> result;
		mapper.mapEvent(axis(Common::JOYSTICK_AXIS_RIGHT_STICK_X, 0, -37, true), result);
		TS_ASSERT_EQUALS(result.size(), 2U);
		TS_ASSERT(!result.front().joystick.hasMouseMotion);
		TS_ASSERT_EQUALS(result.back().customType, 43U);
		TS_ASSERT(result.back().joystick.hasMouseMotion);
		TS_ASSERT_EQUALS(result.back().joystick.mouseMotion, 37);
		TS_ASSERT(result.back().joystick.mouseMotionEnd);
	}

	void test_native_engine_motion_end_follows_all_mapped_axes() {
		Events events;
		Common::Keymapper mapper(&events);
		Common::Keymap *map = keymap(mapper);
		bindEngine(map, "x", 42, Common::JOYSTICK_AXIS_RIGHT_STICK_Y, true);
		bindEngine(map, "y", 43, Common::JOYSTICK_AXIS_RIGHT_STICK_Y, true);
		Common::List<Common::Event> result;
		mapper.mapEvent(axis(Common::JOYSTICK_AXIS_RIGHT_STICK_Y, 1000, 11, true), result);
		TS_ASSERT_EQUALS(result.size(), 2U);
		TS_ASSERT(!result.front().joystick.mouseMotionEnd);
		TS_ASSERT(result.back().joystick.mouseMotionEnd);
	}

	void test_native_engine_hardware_axis_handles_full_negative_range() {
		Events events;
		Common::Keymapper mapper(&events);
		Common::Keymap *map = keymap(mapper);
		bindEngine(map, "look", 42, Common::JOYSTICK_AXIS_RIGHT_STICK_X, false);
		Common::Event event = axis(Common::JOYSTICK_AXIS_RIGHT_STICK_X, Common::JOYAXIS_MIN, 0, true);
		event.joystick.hasMouseMotion = false;
		Common::List<Common::Event> result;
		mapper.mapEvent(event, result);
		TS_ASSERT_EQUALS(result.size(), 1U);
		TS_ASSERT_EQUALS(result.front().joystick.position, Common::JOYAXIS_MAX);
		TS_ASSERT(!result.front().joystick.hasMouseMotion);
	}

	void test_hardware_axes_keep_velocity_mapping() {
		Events events;
		Common::Keymapper mapper(&events);
		Common::Keymap *map = keymap(mapper);
		bind(map, "right", Common::VirtualMouse::kCustomActionVirtualAxisRight, Common::JOYSTICK_AXIS_RIGHT_STICK_X, true);
		Common::Event event = axis(Common::JOYSTICK_AXIS_RIGHT_STICK_X, 16000, 0, true);
		event.joystick.hasMouseMotion = false;
		Common::List<Common::Event> result;
		TS_ASSERT(mapper.mapEvent(event, result));
		TS_ASSERT_EQUALS(result.size(), 1U);
		TS_ASSERT_EQUALS(result.front().type, Common::EVENT_CUSTOM_BACKEND_ACTION_AXIS);
		TS_ASSERT_EQUALS(result.front().joystick.position, 16000);
	}
};
