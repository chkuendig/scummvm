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

class EventsTestSuite : public CxxTest::TestSuite {
public:
	void test_software_axis_data_does_not_leak_to_hardware_events() {
		class Source : public Common::EventSource {
			int _count = 0;
		public:
			bool pollEvent(Common::Event &event) override {
				if (_count++ >= 2) {
					return false;
				}
				event.type = Common::EVENT_JOYAXIS_MOTION;
				event.joystick.axis = Common::JOYSTICK_AXIS_RIGHT_STICK_X;
				event.joystick.position = 16000;
				if (_count == 1) {
					event.joystick.hasMouseMotion = true;
					event.joystick.mouseMotion = 123;
					event.joystick.mouseMotionEnd = true;
				}
				return true;
			}
		} source;
		class Observer : public Common::EventObserver {
		public:
			Common::JoystickState states[2];
			int count = 0;
			bool notifyEvent(const Common::Event &event) override {
				states[count++] = event.joystick;
				return false;
			}
		} observer;
		Common::EventDispatcher dispatcher;
		dispatcher.registerSource(&source, false);
		dispatcher.registerObserver(&observer, 0, false);
		dispatcher.dispatch();
		TS_ASSERT_EQUALS(observer.count, 2);
		TS_ASSERT(observer.states[0].hasMouseMotion);
		TS_ASSERT_EQUALS(observer.states[0].mouseMotion, 123);
		TS_ASSERT(!observer.states[1].hasMouseMotion);
		TS_ASSERT_EQUALS(observer.states[1].mouseMotion, 0);
		TS_ASSERT(!observer.states[1].mouseMotionEnd);
		TS_ASSERT_EQUALS(observer.states[1].position, 16000);
	}

	void test_hotspots_show_event() {
		Common::Event event;
		event.type = Common::EVENT_HOTSPOTS_SHOW;

		TS_ASSERT_EQUALS(event.type, Common::EVENT_HOTSPOTS_SHOW);
	}

	void test_hotspots_hide_event() {
		Common::Event event;
		event.type = Common::EVENT_HOTSPOTS_HIDE;

		TS_ASSERT_EQUALS(event.type, Common::EVENT_HOTSPOTS_HIDE);
	}
};
