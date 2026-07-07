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

#include "common/system.h"

#include "gui/gui-manager.h"

#include "gui/EventRecorder.h"

#include "common/events.h"
#include "common/rect.h"
#include "common/translation.h"

#include "graphics/cursorman.h"

#include "gui/editrecorddialog.h"
#include "gui/ThemeEval.h"

#include "gui/onscreendialog.h"

namespace GUI {

bool OnScreenDialog::isVisible() const {
	return true;
}

enum {
	kStopCmd = 'STOP',
	kEditCmd = 'EDIT',
	kSwitchModeCmd = 'MODE',
	kFastModeCmd = 'FAST'
};

void OnScreenDialog::reflowLayout() {
	Dialog::reflowLayout();

	_x = _y = 0;

#ifdef EMSCRIPTEN
	// Position the panel inside the *game* draw area instead of the window
	// corner. The browser canvas is always fullscreen, so the game is
	// letterboxed into it (black bars) whenever the aspect ratios differ (e.g.
	// a phone in portrait). The pointer is clipped to the game rect, so a panel
	// left in the black bars is visible but unreachable. The game rect is not
	// exposed to the GUI, so estimate it as the aspect-fit-centered game area
	// within the overlay - the same mapping EventRecorder::notifyEvent() uses to
	// translate clicks, so the two stay consistent. Place the panel at the
	// top-center of that area (the center column is inside the rect for any
	// letterboxing). On desktop the game fills its window, so the stock (0,0)
	// window corner is already reachable and this is skipped.
	const int overlayW = g_system->getOverlayWidth();
	const int overlayH = g_system->getOverlayHeight();
	const int gameW = g_system->getWidth();
	const int gameH = g_system->getHeight();
	_x = MAX(0, (overlayW - _w) / 2);
	if (gameW > 0 && gameH > 0) {
		// Aspect-fit height of the game area within the overlay/window.
		int fitH = overlayH;
		if ((int64)overlayW * gameH < (int64)overlayH * gameW)
			fitH = (int)(((int64)overlayW * gameH) / gameW);
		_y = MAX(0, (overlayH - fitH) / 2);
	}
#endif
}

void OnScreenDialog::releaseFocus() {
}

OnScreenDialog::OnScreenDialog(bool isRecord) : Dialog("OnScreenDialog") {
#ifndef DISABLE_FANCY_THEMES
	if (g_gui.xmlEval()->getVar("Globals.OnScreenDialog.ShowPics") == 1 && g_gui.theme()->supportsImages()) {
		GUI::PicButtonWidget *button;

		button = new PicButtonWidget(this, "OnScreenDialog.StopButton", Common::U32String(), kStopCmd, 0);
		if (!g_gui.useLowResGUI())
			button->setGfxFromTheme(ThemeEngine::kImageStopButton);
		else
			button->setGfxFromTheme(ThemeEngine::kImageStopSmallButton);

		if (isRecord) {
			button = new PicButtonWidget(this, "OnScreenDialog.EditButton", Common::U32String(), kEditCmd, 0);
			if (!g_gui.useLowResGUI())
				button->setGfxFromTheme(ThemeEngine::kImageEditButton);
			else
				button->setGfxFromTheme(ThemeEngine::kImageEditSmallButton);
		} else {
			button = new PicButtonWidget(this, "OnScreenDialog.SwitchModeButton", Common::U32String(), kSwitchModeCmd, 0);
			if (!g_gui.useLowResGUI())
				button->setGfxFromTheme(ThemeEngine::kImageSwitchModeButton);
			else
				button->setGfxFromTheme(ThemeEngine::kImageSwitchModeSmallButton);

			button = new PicButtonWidget(this, "OnScreenDialog.FastReplayButton", Common::U32String(), kFastModeCmd, 0);
			if (!g_gui.useLowResGUI())
				button->setGfxFromTheme(ThemeEngine::kImageFastReplayButton);
			else
				button->setGfxFromTheme(ThemeEngine::kImageFastReplaySmallButton);
		}
	} else
#endif
	{
		if (!g_gui.useLowResGUI())
			new ButtonWidget(this, "OnScreenDialog.StopButton", Common::U32String("[ ]"), _("Stop"), kStopCmd);
		else
			new ButtonWidget(this, "OnScreenDialog.StopButton", Common::U32String("[]"), _("Stop"), kStopCmd);

		if (isRecord) {
			new ButtonWidget(this, "OnScreenDialog.EditButton", Common::U32String("E"), _("Edit record description"), kEditCmd);
		} else {
			new ButtonWidget(this, "OnScreenDialog.SwitchModeButton", Common::U32String("G"), _("Switch to Game"), kSwitchModeCmd);

			new ButtonWidget(this, "OnScreenDialog.FastReplayButton", Common::U32String(">>"), _("Fast replay"), kFastModeCmd);
		}
	}


	_text = new GUI::StaticTextWidget(this, "OnScreenDialog.TimeLabel", Common::U32String("00:00:00"));
	_enableDrag = false;
	_mouseOver = false;
	_editDlgShown = false;

	_lastTime = 0;
	_dlg = nullptr;
}

void OnScreenDialog::handleCommand(CommandSender *sender, uint32 cmd, uint32 data) {
	Common::Event eventReturnToLauncher;
	switch (cmd) {
	case kStopCmd:
		eventReturnToLauncher.type = Common::EVENT_RETURN_TO_LAUNCHER;
		g_system->getEventManager()->pushEvent(eventReturnToLauncher);
		close();
		break;
	case kEditCmd:
		_dlg = new EditRecordDialog(g_eventRec.getAuthor(), g_eventRec.getName(), g_eventRec.getNotes());
		CursorMan.lock(false);
		g_eventRec.setRedraw(false);
		g_system->showOverlay();
		_editDlgShown = true;
		_dlg->runModal();
		_editDlgShown = false;
		g_system->hideOverlay();
		g_eventRec.setRedraw(true);
		CursorMan.lock(true);
		g_eventRec.setAuthor(((EditRecordDialog *)_dlg)->getAuthor());
		g_eventRec.setName(((EditRecordDialog *)_dlg)->getName());
		g_eventRec.setNotes(((EditRecordDialog *)_dlg)->getNotes());
		delete _dlg;
		break;
	case kSwitchModeCmd:
		if (g_eventRec.switchMode()) {
			close();
		}
		break;
	case kFastModeCmd:
		g_eventRec.switchFastMode();
		break;
	default:
		break;
	}
}

void OnScreenDialog::setReplayedTime(uint32 newTime) {
	if (newTime - _lastTime > 1000) {
		uint32 seconds = newTime / 1000;
		_text->setLabel(Common::String::format("%.2d:%.2d:%.2d", seconds / 3600 % 24, seconds / 60 % 60, seconds % 60));
		_lastTime = newTime;
	}
}

OnScreenDialog::~OnScreenDialog() {
}

void OnScreenDialog::handleMouseMoved(int x, int y, int button) {
	if (_enableDrag) {
		_x = _x + x - _dragPoint.x;
		_y = _y + y - _dragPoint.y;
	}
	Dialog::handleMouseMoved(x, y, button);
	if (isMouseOver(x, y)) {
		if (_mouseOver == false) {
			g_gui.theme()->showCursor();
			CursorMan.lock(true);
		}
		_mouseOver = true;
	} else {
		if (_mouseOver == true) {
			CursorMan.lock(false);
			g_gui.theme()->hideCursor();
		}
		_mouseOver = false;
	}
}

void OnScreenDialog::handleMouseDown(int x, int y, int button, int clickCount) {
	if (isMouseOver(x, y)) {
		_dragPoint.x = x;
		_dragPoint.y = y;
		_enableDrag = true;
	}
	Dialog::handleMouseDown(x, y, button, clickCount);
}

void OnScreenDialog::handleMouseUp(int x, int y, int button, int clickCount) {
	if (isMouseOver(x, y)) {

	}
	_enableDrag = false;
	Dialog::handleMouseUp(x, y, button, clickCount);
}

bool OnScreenDialog::isMouseOver(int x, int y) {
	return (x >= 0 && x < _w && y >= 0 && y < _h);
}

bool OnScreenDialog::isMouseOver() {
	return _mouseOver;
}

void OnScreenDialog::close() {
	CursorMan.lock(false);
	Dialog::close();
}

Dialog *OnScreenDialog::getActiveDlg() {
	if (_editDlgShown) {
		return _dlg;
	} else {
		return this;
	}
}

bool OnScreenDialog::isEditDlgVisible() {
	return _editDlgShown;
}

}
