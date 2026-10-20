#include <cxxtest/TestSuite.h>

#include "common/touch-mode.h"

class TouchModeTestSuite : public CxxTest::TestSuite {
public:
	void test_gui_overlay_uses_menu_context_in_2d_and_3d() {
		TS_ASSERT_EQUALS(Common::touchContext(false, true), Common::kTouchContextMenus);
		TS_ASSERT_EQUALS(Common::touchContext(true, true), Common::kTouchContextMenus);
		TS_ASSERT_EQUALS(Common::touchContext(false, false), Common::kTouchContext2d);
		TS_ASSERT_EQUALS(Common::touchContext(true, false), Common::kTouchContext3d);
	}

	void test_menu_preset_cannot_enable_gamepad() {
		TS_ASSERT_EQUALS(Common::resolveTouchMode(Common::kTouchModeGamepad, Common::kTouchContextMenus, true), Common::kTouchModeMouse);
		TS_ASSERT_EQUALS(Common::resolveTouchMode(Common::kTouchModeTouchpad, Common::kTouchContextMenus, true), Common::kTouchModeTouchpad);
	}

	void test_temporary_unavailability_preserves_requested_gamepad() {
		const Common::TouchMode requested = Common::kTouchModeGamepad;
		TS_ASSERT_EQUALS(Common::resolveTouchMode(requested, Common::kTouchContext3d, true), Common::kTouchModeGamepad);
		TS_ASSERT_EQUALS(Common::resolveTouchMode(requested, Common::kTouchContext3d, false), Common::kTouchModeTouchpad);
		TS_ASSERT_EQUALS(Common::resolveTouchMode(requested, Common::kTouchContextMenus, false), Common::kTouchModeMouse);
		TS_ASSERT_EQUALS(Common::resolveTouchMode(requested, Common::kTouchContext3d, true), Common::kTouchModeGamepad);
	}

	void test_cycle_skips_gamepad_in_menus_and_without_a_usable_pad() {
		TS_ASSERT_EQUALS(Common::nextTouchMode(Common::kTouchModeMouse, Common::kTouchContextMenus, true), Common::kTouchModeTouchpad);
		TS_ASSERT_EQUALS(Common::nextTouchMode(Common::kTouchModeMouse, Common::kTouchContext2d, false), Common::kTouchModeTouchpad);
		TS_ASSERT_EQUALS(Common::nextTouchMode(Common::kTouchModeMouse, Common::kTouchContext2d, true), Common::kTouchModeGamepad);
		TS_ASSERT_EQUALS(Common::nextTouchMode(Common::kTouchModeGamepad, Common::kTouchContext2d, true), Common::kTouchModeTouchpad);
	}

	void test_context_defaults_and_separate_override_keys() {
		TS_ASSERT_EQUALS(Common::resolveTouchMode(Common::kTouchModeDefault, Common::kTouchContextMenus, true), Common::kTouchModeMouse);
		TS_ASSERT_EQUALS(Common::resolveTouchMode(Common::kTouchModeDefault, Common::kTouchContext2d, true), Common::kTouchModeTouchpad);
		TS_ASSERT_EQUALS(Common::resolveTouchMode(Common::kTouchModeDefault, Common::kTouchContext3d, true), Common::kTouchModeGamepad);
		TS_ASSERT_EQUALS(Common::touchModeKeyForContext(Common::kTouchContextMenus), TOUCH_MODE_MENUS_KEY);
		TS_ASSERT_EQUALS(Common::touchModeKeyForContext(Common::kTouchContext2d), TOUCH_MODE_2D_GAMES_KEY);
		TS_ASSERT_EQUALS(Common::touchModeKeyForContext(Common::kTouchContext3d), TOUCH_MODE_3D_GAMES_KEY);
	}
};
