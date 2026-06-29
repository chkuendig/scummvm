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

#include "hpl1/metaengine.h"
#include "backends/keymapper/action.h"
#include "backends/keymapper/keymap.h"
#include "common/savefile.h"
#include "common/system.h"
#include "common/translation.h"
#include "graphics/scaler.h"
#include "graphics/thumbnail.h"
#include "hpl1/detection.h"
#include "hpl1/graphics.h"
#include "hpl1/hpl1.h"

const char *Hpl1MetaEngine::getName() const {
	return "hpl1";
}

Common::Error Hpl1MetaEngine::createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const {
	*engine = new Hpl1::Hpl1Engine(syst, desc);
	return Common::kNoError;
}

bool Hpl1MetaEngine::hasFeature(MetaEngineFeature f) const {
	return checkExtendedSaves(f) ||
		   (f == kSupportsLoadingDuringStartup);
}

void Hpl1MetaEngine::getSavegameThumbnail(Graphics::Surface &thumbnail) {
	Common::ScopedPtr<Graphics::Surface> screen = Hpl1::createViewportScreenshot();
	Common::ScopedPtr<Graphics::Surface> scaledScreen(screen->scale(kThumbnailWidth, kThumbnailHeight2));
	scaledScreen->convertToInPlace(Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
	thumbnail.copyFrom(*scaledScreen);
	screen->free();
	scaledScreen->free();
}

Common::Action *createKeyBoardAction(const char *id, const Common::U32String &desc, const char *defaultMap, const Common::KeyState &key) {
	Common::Action *act = new Common::Action(id, desc);
	act->setKeyEvent(key);
	act->addDefaultInputMapping(defaultMap);
	return act;
}

Common::Action *createMouseAction(const char *id, const Common::U32String &desc, const char *defaultMap, const Common::EventType type) {
	Common::Action *act = new Common::Action(id, desc);
	act->setEvent(type);
	act->addDefaultInputMapping(defaultMap);
	return act;
}

// The right analog stick drives the backend virtual mouse so it can control the
// (relative) mouse-look camera, while the left stick is reserved for movement.
// These custom action ids must match the (private) values of the
// Common::VirtualMouse::kCustomActionVirtual* enum in
// backends/keymapper/virtual-mouse.h; the VirtualMouse event observer picks the
// events up and turns them into mouse-move (relMouse) events.
enum {
	kVirtualMouseUp = 10000,
	kVirtualMouseDown = 10001,
	kVirtualMouseLeft = 10002,
	kVirtualMouseRight = 10003
};

Common::Action *createVirtualMouseAxisAction(const char *id, const Common::U32String &desc, const char *defaultMap, const Common::CustomEventType type) {
	Common::Action *act = new Common::Action(id, desc);
	act->setCustomBackendActionAxisEvent(type);
	act->addDefaultInputMapping(defaultMap);
	return act;
}

Common::Array<Common::Keymap *> Hpl1MetaEngine::initKeymaps(const char *target) const {
	using Common::Keymap;
	using namespace Hpl1;

	Common::Action *act;

	Keymap *movement = new Keymap(Keymap::kKeymapTypeGame, "HPL1_MOVEMENT", "Movement");
	// Left analog stick: walk / strafe.
	act = createKeyBoardAction("FORWARD", _("Forward"), "w", Common::KEYCODE_w);
	act->addDefaultInputMapping("JOY_LEFT_STICK_Y-");
	act->addDefaultInputMapping("JOY_UP");
	movement->addAction(act);
	act = createKeyBoardAction("BACKWARD", _("Backward"), "s", Common::KEYCODE_s);
	act->addDefaultInputMapping("JOY_LEFT_STICK_Y+");
	act->addDefaultInputMapping("JOY_DOWN");
	movement->addAction(act);
	act = createKeyBoardAction("LEFT", _("Strafe left"), "a", Common::KEYCODE_a);
	act->addDefaultInputMapping("JOY_LEFT_STICK_X-");
	act->addDefaultInputMapping("JOY_LEFT");
	movement->addAction(act);
	act = createKeyBoardAction("RIGHT", _("Strafe right"), "d", Common::KEYCODE_d);
	act->addDefaultInputMapping("JOY_LEFT_STICK_X+");
	act->addDefaultInputMapping("JOY_RIGHT");
	movement->addAction(act);
	act = createKeyBoardAction("LEAN_LEFT", _("Lean left"), "q", Common::KEYCODE_q);
	movement->addAction(act);
	act = createKeyBoardAction("LEAN_RIGHT", _("Lean right"), "e", Common::KEYCODE_e);
	movement->addAction(act);
	act = createKeyBoardAction("RUN", _("Run"), "LSHIFT", Common::KEYCODE_LSHIFT);
	movement->addAction(act);
	act = createKeyBoardAction("JUMP", _("Jump"), "SPACE", Common::KEYCODE_SPACE);
	act->addDefaultInputMapping("JOY_X");
	movement->addAction(act);
	act = createKeyBoardAction("CROUCH", _("Crouch"), "LCTRL", Common::KEYCODE_LCTRL);
	act->addDefaultInputMapping("JOY_Y");
	movement->addAction(act);
	// Right analog stick: mouse-look camera (via the backend virtual mouse).
	movement->addAction(createVirtualMouseAxisAction("LOOK_UP", _("Look up"), "JOY_RIGHT_STICK_Y-", kVirtualMouseUp));
	movement->addAction(createVirtualMouseAxisAction("LOOK_DOWN", _("Look down"), "JOY_RIGHT_STICK_Y+", kVirtualMouseDown));
	movement->addAction(createVirtualMouseAxisAction("LOOK_LEFT", _("Look left"), "JOY_RIGHT_STICK_X-", kVirtualMouseLeft));
	movement->addAction(createVirtualMouseAxisAction("LOOK_RIGHT", _("Look right"), "JOY_RIGHT_STICK_X+", kVirtualMouseRight));

	Keymap *actions = new Keymap(Keymap::kKeymapTypeGame, "HPL1_ACTIONS", "Actions");
	act = createKeyBoardAction("INTERACTMODE", _("Interact mode"), "r", Common::KEYCODE_r);
	actions->addAction(act);
	act = createMouseAction("LOOK_MODE", _("Look mode"), "MOUSE_MIDDLE", Common::EVENT_MBUTTONDOWN);
	actions->addAction(act);
	act = createKeyBoardAction("HOLSTER", _("Holster"), "x", Common::KEYCODE_x);
	actions->addAction(act);
	act = createMouseAction("EXAMINE", _("Examine"), "MOUSE_LEFT", Common::EVENT_LBUTTONDOWN);
	act->addDefaultInputMapping("JOY_A");
	actions->addAction(act);
	act = createMouseAction("INTERACT", _("Interact"), "MOUSE_RIGHT", Common::EVENT_RBUTTONDOWN);
	act->addDefaultInputMapping("JOY_B");
	actions->addAction(act);

	Keymap *misc = new Keymap(Keymap::kKeymapTypeGame, "HPL1_MISC", "Misc");
	act = createKeyBoardAction("INVENTORY", _("Inventory"), "TAB", Common::KEYCODE_TAB);
	act->addDefaultInputMapping("JOY_RIGHT_SHOULDER");
	misc->addAction(act);
	act = createKeyBoardAction("NOTEBOOK", _("Notebook"), "n", Common::KEYCODE_n);
	misc->addAction(act);
	misc->addAction(createKeyBoardAction("PERSONAL_NOTES", _("Personal notes"), "p", Common::KEYCODE_p));
	act = createKeyBoardAction("FLASHLIGHT", _("Flashlight"), "f", Common::KEYCODE_f);
	act->addDefaultInputMapping("JOY_LEFT_SHOULDER");
	misc->addAction(act);
	misc->addAction(createKeyBoardAction("GLOWSTICK", _("Glowstick"), "g", Common::KEYCODE_g));
	// Esc opens the in-game menu and skips the intro/cutscenes; it is read by the
	// engine as a raw keyboard event. Expose it through the keymapper as well so a
	// gamepad Start button can do the same.
	act = createKeyBoardAction("MENU", _("Menu / Skip"), "ESCAPE", Common::KEYCODE_ESCAPE);
	act->addDefaultInputMapping("JOY_START");
	misc->addAction(act);

	Common::Array<Common::Keymap *> keymaps(3);
	keymaps[0] = movement;
	keymaps[1] = actions;
	keymaps[2] = misc;
	return keymaps;
}

#if PLUGIN_ENABLED_DYNAMIC(HPL1)
REGISTER_PLUGIN_DYNAMIC(HPL1, PLUGIN_TYPE_ENGINE, Hpl1MetaEngine);
#else
REGISTER_PLUGIN_STATIC(HPL1, PLUGIN_TYPE_ENGINE, Hpl1MetaEngine);
#endif
