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

// Disable symbol overrides so that we can use system headers.
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "gui/gui-manager.h"
#include "gui/message.h"
#include "gui/ThemeEval.h"
#include "gui/widget.h"
#include "gui/widgets/popup.h"

#include "common/translation.h"
#include "backends/platform/ios7/ios7_osys_main.h"

#include <TargetConditionals.h>

enum {
	kGamepadControllerOpacityChanged = 'gcoc',
};

class IOS7OptionsWidget final : public GUI::OptionsContainerWidget {
public:
	explicit IOS7OptionsWidget(GuiObject *boss, const Common::String &name, const Common::String &domain);
	~IOS7OptionsWidget() override;

	// OptionsContainerWidget API
	void load() override;
	bool save() override;
	bool hasKeys() override;
	void setEnabled(bool e) override;

private:
	// OptionsContainerWidget API
	void defineLayout(GUI::ThemeEval &layouts, const Common::String &layoutName, const Common::String &overlayedLayout) const override;
	void handleCommand(GUI::CommandSender *sender, uint32 cmd, uint32 data) override;
	uint32 loadDirectionalInput(const Common::String &setting, bool acceptDefault, uint32 defaultValue);
	void saveDirectionalInput(const Common::String &setting, uint32 input);

#if TARGET_OS_IOS
	uint32 loadOrientation(const Common::String &setting, bool acceptDefault, uint32 defaultValue);
	void saveOrientation(const Common::String &setting, uint32 orientation);
#endif

	GUI::CheckboxWidget *_gamepadControllerCheckbox;
	GUI::StaticTextWidget *_gamepadControllerOpacityDesc;
	GUI::SliderWidget *_gamepadControllerOpacitySlider;
	GUI::StaticTextWidget *_gamepadControllerOpacityLabel;
	GUI::StaticTextWidget *_gamepadControllerDirectionalInputDesc;
	GUI::PopUpWidget *_gamepadControllerDirectionalInputPopUp;
	GUI::CheckboxWidget *_gamepadControllerMinimalLayoutCheckbox;

	GUI::CheckboxWidget *_keyboardFnBarCheckbox;
#if TARGET_OS_IOS
	// The per-context touch-mode dropdowns and the on-screen-control checkbox are
	// now provided by the shared Control tab (gui/options.cpp); iOS opts in via
	// kFeatureTouchpadMode. Only the iOS-specific settings remain here.
	GUI::StaticTextWidget *_orientationDesc;
	GUI::StaticTextWidget *_orientationMenusDesc;
	GUI::PopUpWidget *_orientationMenusPopUp;
	GUI::StaticTextWidget *_orientationGamesDesc;
	GUI::PopUpWidget *_orientationGamesPopUp;
#endif
	bool _enabled;
};

IOS7OptionsWidget::IOS7OptionsWidget(GuiObject *boss, const Common::String &name, const Common::String &domain) :
		OptionsContainerWidget(boss, name, "IOS7OptionsDialog", domain), _enabled(true) {

	_gamepadControllerCheckbox = new GUI::CheckboxWidget(widgetsBoss(), "IOS7OptionsDialog.GamepadController", _("Show Gamepad Controller (iOS 15 and later)"));
	_gamepadControllerOpacityDesc = new GUI::StaticTextWidget(widgetsBoss(), "IOS7OptionsDialog.GamepadControllerOpacity", _("Gamepad opacity"));
	_gamepadControllerOpacitySlider = new GUI::SliderWidget(widgetsBoss(), "IOS7OptionsDialog.GamepadControllerOpacitySlider", _("Gamepad opacity"), kGamepadControllerOpacityChanged);
	_gamepadControllerOpacityLabel = new GUI::StaticTextWidget(widgetsBoss(), "IOS7OptionsDialog.GamepadControllerOpacityLabel", Common::U32String(" "), Common::U32String(), GUI::ThemeEngine::kFontStyleBold, Common::UNK_LANG, false);
	_gamepadControllerOpacitySlider->setMinValue(1);
	_gamepadControllerOpacitySlider->setMaxValue(10);
	_gamepadControllerDirectionalInputDesc = new GUI::StaticTextWidget(widgetsBoss(), "IOS7OptionsDialog.GamepadControllerLeftButton", _("Directional button:"));
	_gamepadControllerDirectionalInputPopUp = new GUI::PopUpWidget(widgetsBoss(), "IOS7OptionsDialog.GamepadControllerLeftButtonPopUp");
	_gamepadControllerDirectionalInputPopUp->appendEntry(_("Thumbstick"), kDirectionalInputThumbstick);
	_gamepadControllerDirectionalInputPopUp->appendEntry(_("Dpad"), kDirectionalInputDpad);
	_gamepadControllerMinimalLayoutCheckbox = new GUI::CheckboxWidget(widgetsBoss(), "IOS7OptionsDialog.GamepadControllerMinimalLayout", _("Use minimal gamepad layout"));

	_keyboardFnBarCheckbox = new GUI::CheckboxWidget(widgetsBoss(), "IOS7OptionsDialog.KeyboardFunctionBar", _("Show keyboard function bar"));

#if TARGET_OS_IOS
	const bool inAppDomain = domain.equalsIgnoreCase(Common::ConfigManager::kApplicationDomain);

	_orientationDesc = new GUI::StaticTextWidget(widgetsBoss(), "IOS7OptionsDialog.OrientationText", _("Select the orientation:"));
	if (inAppDomain) {
		_orientationMenusDesc = new GUI::StaticTextWidget(widgetsBoss(), "IOS7OptionsDialog.OMenusText", _("In menus"));
		_orientationMenusPopUp = new GUI::PopUpWidget(widgetsBoss(), "IOS7OptionsDialog.OMenus");
		_orientationMenusPopUp->appendEntry(_("Automatic"), kScreenOrientationAuto);
		_orientationMenusPopUp->appendEntry(_("Portrait"), kScreenOrientationPortrait);
		_orientationMenusPopUp->appendEntry(_("Landscape"), kScreenOrientationLandscape);
	} else {
		_orientationMenusDesc = nullptr;
		_orientationMenusPopUp = nullptr;
	}

	_orientationGamesDesc = new GUI::StaticTextWidget(widgetsBoss(), "IOS7OptionsDialog.OGamesText", _("In games"));
	_orientationGamesPopUp = new GUI::PopUpWidget(widgetsBoss(), "IOS7OptionsDialog.OGames");

	if (!inAppDomain) {
		_orientationGamesPopUp->appendEntry(_("<default>"), kScreenOrientationAuto);
	}

	_orientationGamesPopUp->appendEntry(_("Automatic"), kScreenOrientationAuto);
	_orientationGamesPopUp->appendEntry(_("Portrait"), kScreenOrientationPortrait);
	_orientationGamesPopUp->appendEntry(_("Landscape"), kScreenOrientationLandscape);
#endif

	// setEnabled is normally only called from the EditGameDialog, but some options (GamepadController)
	// should be disabled in all domains if system is running a lower version of iOS than 15.0.
	setEnabled(_enabled);
}

IOS7OptionsWidget::~IOS7OptionsWidget() {
}

void IOS7OptionsWidget::defineLayout(GUI::ThemeEval &layouts, const Common::String &layoutName, const Common::String &overlayedLayout) const {

	layouts.addDialog(layoutName, overlayedLayout)
	        .addLayout(GUI::ThemeLayout::kLayoutVertical)
	            .addWidget("GamepadController", "Checkbox")
			.addLayout(GUI::ThemeLayout::kLayoutHorizontal)
				.addPadding(0, 0, 0, 0)
				.addWidget("GamepadControllerLeftButton", "OptionsLabel")
				.addWidget("GamepadControllerLeftButtonPopUp", "PopUp")
			.closeLayout()
	        .addLayout(GUI::ThemeLayout::kLayoutHorizontal)
	            .addPadding(0, 0, 0, 0)
	            .addWidget("GamepadControllerOpacity", "OptionsLabel")
	            .addWidget("GamepadControllerOpacitySlider", "Slider")
	            .addWidget("GamepadControllerOpacityLabel", "OptionsLabel")
	        .closeLayout()
	            .addWidget("GamepadControllerMinimalLayout", "Checkbox")
                .addWidget("KeyboardFunctionBar", "Checkbox");
#if TARGET_OS_IOS
	const bool inAppDomain = _domain.equalsIgnoreCase(Common::ConfigManager::kApplicationDomain);

	layouts.addWidget("OrientationText", "", -1, layouts.getVar("Globals.Line.Height"));
	if (inAppDomain) {
		layouts.addLayout(GUI::ThemeLayout::kLayoutHorizontal)
			.addPadding(0, 0, 0, 0)
			.addWidget("OMenusText", "OptionsLabel")
			.addWidget("OMenus", "PopUp")
		.closeLayout();
	}
	layouts.addLayout(GUI::ThemeLayout::kLayoutHorizontal)
			.addPadding(0, 0, 0, 0)
			.addWidget("OGamesText", "OptionsLabel")
			.addWidget("OGames", "PopUp")
		.closeLayout();
#endif
	layouts.closeLayout()
	    .closeDialog();
}

void IOS7OptionsWidget::handleCommand(GUI::CommandSender *sender, uint32 cmd, uint32 data) {
	switch (cmd) {
	case kGamepadControllerOpacityChanged: {
		const int newValue = _gamepadControllerOpacitySlider->getValue();
		_gamepadControllerOpacityLabel->setValue(newValue);
		_gamepadControllerOpacityLabel->markAsDirty();
		break;
	}
	default:
		GUI::OptionsContainerWidget::handleCommand(sender, cmd, data);
	}
}

uint32 IOS7OptionsWidget::loadDirectionalInput(const Common::String &setting, bool acceptDefault, uint32 defaultValue) {
	if (!acceptDefault || ConfMan.hasKey(setting, _domain)) {
		Common::String input = ConfMan.get(setting, _domain);
		if (input == "thumbstick") {
			return kDirectionalInputThumbstick;
		} else if (input == "dpad") {
			return kScreenOrientationPortrait;
		} else {
			return defaultValue;
		}
	} else {
		return kDirectionalInputThumbstick;
	}
}

void IOS7OptionsWidget::saveDirectionalInput(const Common::String &setting, uint32 input) {
	switch (input) {
	case kDirectionalInputThumbstick:
		ConfMan.set(setting, "thumbstick", _domain);
		break;
	case kDirectionalInputDpad:
		ConfMan.set(setting, "dpad", _domain);
		break;
	default:
		// default
		ConfMan.removeKey(setting, _domain);
		break;
	}
}

#if TARGET_OS_IOS
uint32 IOS7OptionsWidget::loadOrientation(const Common::String &setting, bool acceptDefault, uint32 defaultValue) {
	if (!acceptDefault || ConfMan.hasKey(setting, _domain)) {
		Common::String orientation = ConfMan.get(setting, _domain);
		if (orientation == "auto") {
			return kScreenOrientationAuto;
		} else if (orientation == "portrait") {
			return kScreenOrientationPortrait;
		} else if (orientation == "landscape") {
			return kScreenOrientationLandscape;
		} else {
			return defaultValue;
		}
	} else {
		return kScreenOrientationAuto;
	}
}

void IOS7OptionsWidget::saveOrientation(const Common::String &setting, uint32 orientation) {
	switch (orientation) {
	case kScreenOrientationAuto:
		ConfMan.set(setting, "auto", _domain);
		break;
	case kScreenOrientationPortrait:
		ConfMan.set(setting, "portrait", _domain);
		break;
	case kScreenOrientationLandscape:
		ConfMan.set(setting, "landscape", _domain);
		break;
	default:
		// default
		ConfMan.removeKey(setting, _domain);
		break;
	}
}
#endif

void IOS7OptionsWidget::load() {
	const bool inAppDomain = _domain.equalsIgnoreCase(Common::ConfigManager::kApplicationDomain);

	_gamepadControllerCheckbox->setState(ConfMan.getBool("gamepad_controller", _domain));
	_gamepadControllerOpacitySlider->setValue(ConfMan.getInt("gamepad_controller_opacity", _domain));
	_gamepadControllerOpacityLabel->setValue(_gamepadControllerOpacitySlider->getValue());
	_gamepadControllerDirectionalInputPopUp->setSelectedTag(loadDirectionalInput("gamepad_controller_directional_input", !inAppDomain, kDirectionalInputThumbstick));
	_gamepadControllerMinimalLayoutCheckbox->setState(ConfMan.getBool("gamepad_controller_minimal_layout", _domain));

	_keyboardFnBarCheckbox->setState(ConfMan.getBool("keyboard_fn_bar", _domain));

#if TARGET_OS_IOS
	if (inAppDomain) {
		_orientationMenusPopUp->setSelectedTag(loadOrientation("orientation_menus", !inAppDomain, kScreenOrientationAuto));
	}
	_orientationGamesPopUp->setSelectedTag(loadOrientation("orientation_games", !inAppDomain, kScreenOrientationAuto));
#endif
}

bool IOS7OptionsWidget::save() {
#if TARGET_OS_IOS
	const bool inAppDomain = _domain.equalsIgnoreCase(Common::ConfigManager::kApplicationDomain);
#endif

	if (_enabled) {
		ConfMan.setBool("gamepad_controller", _gamepadControllerCheckbox->getState(), _domain);
		ConfMan.setInt("gamepad_controller_opacity", _gamepadControllerOpacitySlider->getValue(), _domain);
		ConfMan.setInt("gamepad_controller_directional_input", _gamepadControllerDirectionalInputPopUp->getSelectedTag(), _domain);
		ConfMan.setBool("gamepad_controller_minimal_layout", _gamepadControllerMinimalLayoutCheckbox->getState(), _domain);

		ConfMan.setBool("keyboard_fn_bar", _keyboardFnBarCheckbox->getState(), _domain);

#if TARGET_OS_IOS
		if (inAppDomain) {
			saveOrientation("orientation_menus", _orientationMenusPopUp->getSelectedTag());
		}
		saveOrientation("orientation_games", _orientationGamesPopUp->getSelectedTag());
#endif
	} else {
		ConfMan.removeKey("gamepad_controller", _domain);
		ConfMan.removeKey("gamepad_controller_opacity", _domain);
		ConfMan.removeKey("gamepad_controller_directional_input", _domain);
		ConfMan.removeKey("gamepad_controller_minimal_layout", _domain);

#if TARGET_OS_IOS
		ConfMan.removeKey("keyboard_fn_bar", _domain);

		if (inAppDomain) {
			ConfMan.removeKey("orientation_menus", _domain);
		}
		ConfMan.removeKey("orientation_games", _domain);
#endif
	}

	return true;
}

bool IOS7OptionsWidget::hasKeys() {
	bool hasKeys = ConfMan.hasKey("gamepad_controller", _domain) ||
	ConfMan.hasKey("gamepad_controller_opacity", _domain) ||
	ConfMan.hasKey("gamepad_controller_directional_input", _domain) ||
	ConfMan.hasKey("gamepad_controller_minimal_layout", _domain);

#if TARGET_OS_IOS
	hasKeys = hasKeys || (_domain.equalsIgnoreCase(Common::ConfigManager::kApplicationDomain) && ConfMan.hasKey("orientation_menus", _domain)) ||
	ConfMan.hasKey("orientation_games", _domain);
#endif

	return hasKeys;
}

void IOS7OptionsWidget::setEnabled(bool e) {
	_enabled = e;

#if TARGET_OS_IOS
#if __IPHONE_15_0
	// On-screen controls (virtual controller is supported in iOS 15 and later)
	if (@available(iOS 15.0, *)) {
		_gamepadControllerCheckbox->setEnabled(e);
		_gamepadControllerDirectionalInputPopUp->setEnabled(e);
		_gamepadControllerOpacityDesc->setEnabled(e);
		_gamepadControllerOpacitySlider->setEnabled(e);
		_gamepadControllerOpacityLabel->setEnabled(e);
		_gamepadControllerMinimalLayoutCheckbox->setEnabled(e);
	} else {
		_gamepadControllerCheckbox->setEnabled(false);
		_gamepadControllerDirectionalInputPopUp->setEnabled(false);
		_gamepadControllerOpacityDesc->setEnabled(false);
		_gamepadControllerOpacitySlider->setEnabled(false);
		_gamepadControllerOpacityLabel->setEnabled(false);
		_gamepadControllerMinimalLayoutCheckbox->setEnabled(false);
	}
#endif /* __IPHONE_15_0  */
#else /* TARGET_OS_IOS */
	_gamepadControllerCheckbox->setEnabled(false);
	_gamepadControllerDirectionalInputDesc->setEnabled(false);
	_gamepadControllerDirectionalInputPopUp->setEnabled(false);
	_gamepadControllerOpacityDesc->setEnabled(false);
	_gamepadControllerOpacitySlider->setEnabled(false);
	_gamepadControllerOpacityLabel->setEnabled(false);
	_gamepadControllerMinimalLayoutCheckbox->setEnabled(false);
#endif /* TARGET_OS_IOS */

	_keyboardFnBarCheckbox->setEnabled(e);

#if TARGET_OS_IOS
	const bool inAppDomain = _domain.equalsIgnoreCase(Common::ConfigManager::kApplicationDomain);

	if (inAppDomain) {
		_orientationMenusDesc->setEnabled(e);
		_orientationMenusPopUp->setEnabled(e);
	}
	_orientationGamesDesc->setEnabled(e);
	_orientationGamesPopUp->setEnabled(e);
#endif
}

GUI::OptionsContainerWidget *OSystem_iOS7::buildBackendOptionsWidget(GUI::GuiObject *boss, const Common::String &name, const Common::String &target) const {
	return new IOS7OptionsWidget(boss, name, target);
}

void OSystem_iOS7::registerDefaultSettings(const Common::String &target) const {
	ConfMan.registerDefault("gamepad_controller", false);
	ConfMan.registerDefault("gamepad_controller_opacity", 6);
	ConfMan.registerDefault("gamepad_controller_directional_input", kDirectionalInputThumbstick);
	ConfMan.registerDefault("gamepad_controller_minimal_layout", false);

	// Touch-mode presets are shared with the Control tab (gui/options.cpp).
	ConfMan.registerDefault(TOUCH_MODE_MENUS_KEY, "direct");
	ConfMan.registerDefault(TOUCH_MODE_2D_GAMES_KEY, "touchpad");
	ConfMan.registerDefault(TOUCH_MODE_3D_GAMES_KEY, "gamepad");

	ConfMan.registerDefault("keyboard_fn_bar", isiOSAppOnMac() ? false : true);

#if TARGET_OS_IOS
	ConfMan.registerDefault("orientation_menus", "auto");
	ConfMan.registerDefault("orientation_games", "auto");

	ConfMan.registerDefault(ONSCREEN_CONTROL_KEY, isiOSAppOnMac() ? false : true);
#endif
}

void OSystem_iOS7::applyBackendSettings() {
	virtualController(ConfMan.getBool("gamepad_controller"));
	// _currentTouchMode is applied by the graphic manager

#if TARGET_OS_IOS
	applyOrientationSettings();
	updateTouchMode();
	if (isKeyboardShown()) {
		setShowKeyboard(false);
		setShowKeyboard(true);
	}
#endif
}

#if TARGET_OS_IOS
void OSystem_iOS7::applyOrientationSettings() {
	const Common::String activeDomain = ConfMan.getActiveDomainName();
	const bool inAppDomain = activeDomain.empty() ||
		activeDomain.equalsIgnoreCase(Common::ConfigManager::kApplicationDomain);

	Common::String setting;

	if (inAppDomain) {
		setting = "orientation_menus";
	} else {
		setting = "orientation_games";
	}

	Common::String orientation = ConfMan.get(setting);
	if (orientation == "portrait") {
		setSupportedScreenOrientation(kScreenOrientationPortrait);
	} else if (orientation == "landscape") {
		setSupportedScreenOrientation(kScreenOrientationLandscape);
	} else {
		setSupportedScreenOrientation(kScreenOrientationAuto);
	}
}
#endif

void OSystem_iOS7::applyTouchSettings(bool _3dMode, bool overlayShown) {
#if TARGET_OS_IOS
	Common::String setting;

	if (overlayShown) {
		setting = TOUCH_MODE_MENUS_KEY;
	} else if (_3dMode) {
		setting = TOUCH_MODE_3D_GAMES_KEY;
	} else {
		setting = TOUCH_MODE_2D_GAMES_KEY;
	}

	_currentTouchMode = Common::parseTouchMode(ConfMan.get(setting), Common::kTouchModeTouchpad);
	if (_currentTouchMode == Common::kTouchModeGamepad) {
		// The on-screen gamepad is only available in-game and while no physical
		// controller is connected; otherwise fall back to touchpad.
		if (overlayShown || iOS7_isControllerConnected())
			_currentTouchMode = Common::kTouchModeTouchpad;
	}

	// updateTouchMode() (dis)connects the native virtual controller to match.
	updateTouchMode();
#else
	(void)_3dMode;
	(void)overlayShown;
#endif
}
