/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "asylum/puzzles/tictactoe.h"

#include "asylum/resources/worldstats.h"

#include "asylum/system/cursor.h"
#include "asylum/system/screen.h"

#include "asylum/views/scene.h"

#include "asylum/asylum.h"

namespace Asylum {

extern int g_debugPolygons;

const int16 puzzleTicTacToePolygons[36][2] = {
	{ 27, 381}, {172, 368}, {190, 474}, { 36, 476},
	{176, 362}, {294, 328}, {331, 456}, {191, 472},
	{304, 327}, {426, 306}, {457, 440}, {340, 460},
	{ 26, 257}, {151, 238}, {169, 356}, { 27, 373},
	{162, 234}, {275, 214}, {299, 321}, {173, 355},
	{283, 210}, {403, 173}, {437, 294}, {305, 317},
	{ 22, 120}, {132, 126}, {146, 223}, { 25, 247},
	{144, 119}, {247,  87}, {268, 205}, {159, 222},
	{259,  84}, {380,  73}, {405, 169}, {281, 201}
};

const int16 puzzleTicTacToePositions[9][2] = {
	{ 62, 367}, {193, 343}, {329, 319},
	{ 38, 247}, {167, 217}, {296, 193},
	{ 19, 121}, {147,  98}, {269,  70}
};

static const struct {
	uint32 field1;
	uint32 field2;
	uint32 field3;
	uint32 strikeOutPositionX;
	uint32 strikeOutPositionO;
	uint32 frameCount;
} puzzleTicTacToeFieldsToCheck[8] = {
	{0, 1, 2, 1,  9, 14},
	{3, 4, 5, 2, 10, 14},
	{6, 7, 8, 3, 11, 14},
	{0, 3, 6, 4, 12, 10},
	{8, 5, 2, 6, 14, 10},
	{0, 4, 8, 8, 16,  4},
	{4, 1, 7, 5, 13, 10},
	{4, 6, 2, 7, 15,  4}
};

PuzzleTicTacToe::PuzzleTicTacToe(AsylumEngine *engine) : Puzzle(engine) {
	_ticker = 0;
	_frameIndex = 0;
	_frameCount = 0;
	_lastMarkedField = 0;
	_needToInitialize = false;
	_strikeOutPosition = 0;

	// Field
	memset(&_gameField, 0, sizeof(_gameField));
	memset(&_field, 0, sizeof(_field));
	_emptyCount = 0;

	_counter = 0;
	_counter2 = 0;
}

PuzzleTicTacToe::~PuzzleTicTacToe() {
}

//////////////////////////////////////////////////////////////////////////
// Event Handling
//////////////////////////////////////////////////////////////////////////
bool PuzzleTicTacToe::init(const AsylumEvent &)  {
	_ticker = 0;
	_vm->clearGameFlag(kGameFlag114);
	_vm->clearGameFlag(kGameFlag215);
	_frameIndex = 0;
	_lastMarkedField = -1;
	_needToInitialize = false;
	_strikeOutPosition = -1;

	getScreen()->setPalette(getWorld()->graphicResourceIds[3]);
	getScreen()->setGammaLevel(getWorld()->graphicResourceIds[3]);

	getCursor()->show();
	getCursor()->set(getWorld()->graphicResourceIds[12], 4, kCursorAnimationLinear, 4);

	initField();

	return true;
}

bool PuzzleTicTacToe::update(const AsylumEvent &)  {
	if (_ticker) {
		++_ticker;

		if (_ticker <= 25) {
			if (_ticker > 20) {
				if (check())
					placeOpponentMark();

				_ticker = 0;
			}
		} else {
			if (_ticker > 40) {
				getSound()->playSound(getWorld()->soundResourceIds[13], false, Config.sfxVolume - 100);

				_ticker = 0;
			}
		}
	}

	getScreen()->draw(getWorld()->graphicResourceIds[0]);
	drawField();
	getScene()->updateAmbientSounds();

	getScreen()->copyBackBufferToScreen();

	return true;
}

bool PuzzleTicTacToe::mouseLeftDown(const AsylumEvent &evt) {
	if (!_vm->isGameFlagNotSet(kGameFlag215) || !_vm->isGameFlagNotSet(kGameFlag114)) {
		getCursor()->show();
		exitPuzzle();
		return true;
	}

	if (_needToInitialize) {
		_needToInitialize = false;
		_frameIndex = 0;
		_lastMarkedField = -1;
		_strikeOutPosition = -1;
		initField();

		return true;
	}

	for (uint32 i = 0; i < ARRAYSIZE(_gameField); i++) {
		if (hitTest(&puzzleTicTacToePolygons[i * 4], evt.mouse, 0)) {
			if (_gameField[i] == ' ') {
				getSound()->playSound(getWorld()->soundResourceIds[11], false, Config.sfxVolume - 100);
				_gameField[i] = 'X';
				_lastMarkedField = i;
				_frameIndex = 0;

				getCursor()->hide();
			}
		}
	}

	return true;
}

bool PuzzleTicTacToe::mouseRightDown(const AsylumEvent &) {
	exitPuzzle();

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Init & update
//////////////////////////////////////////////////////////////////////////
void PuzzleTicTacToe::initField() {
	_counter2 = 0;
	memset(&_gameField, 32, sizeof(_gameField)); // ' ' == 32
}

void PuzzleTicTacToe::drawField() {
	if (_counter > 0) {
		--_counter;

		if (_counter < 2) {
			getCursor()->show();
			exitPuzzle();
			_counter = 0;
			return;
		}
	}

	if (g_debugPolygons) {
		for (uint32 p = 0; p < ARRAYSIZE(puzzleTicTacToePolygons) - 4; p += 4) {
			getScreen()->drawLine(&puzzleTicTacToePolygons[p],     &puzzleTicTacToePolygons[p + 1]);
			getScreen()->drawLine(&puzzleTicTacToePolygons[p + 1], &puzzleTicTacToePolygons[p + 2]);
			getScreen()->drawLine(&puzzleTicTacToePolygons[p + 2], &puzzleTicTacToePolygons[p + 3]);
			getScreen()->drawLine(&puzzleTicTacToePolygons[p + 3], &puzzleTicTacToePolygons[p]);
		}
	}

	// Draw X & O
	for (int32 i = 0; i < ARRAYSIZE(puzzleTicTacToePositions); i++) {
		char mark = _gameField[i];
		Common::Point point = Common::Point(puzzleTicTacToePositions[i][0], puzzleTicTacToePositions[i][1]);

		if (_lastMarkedField == i) {
			if (mark == 'O')
				getScreen()->draw(getWorld()->graphicResourceIds[2], _frameIndex, point);
			else if (mark == 'X')
				getScreen()->draw(getWorld()->graphicResourceIds[1], _frameIndex, point);

			// Update _frameIndex
			++_frameIndex;
			if (_frameIndex > 14 && mark == 'X') {
				_lastMarkedField = -1;
				_frameIndex = 0;
				_ticker = 1;

				if (checkWinner())
					_needToInitialize = true;
			}

			if (_frameIndex > 12 && mark == 'O') {
				_lastMarkedField = -1;
				_frameIndex = 0;

				if (!checkWinner() || !checkWinnerHelper())
					getCursor()->show();
			}
		} else {
			if (mark == 'O')
				getScreen()->draw(getWorld()->graphicResourceIds[2], 12, point);
			else if (mark == 'X')
				getScreen()->draw(getWorld()->graphicResourceIds[1], 14, point);
		}
	}

	if (_lastMarkedField == -1 && checkWinner())
		_needToInitialize = true;

	// Draw strikeOut
	if (_strikeOutPosition > 0 && !_ticker) {
		switch (_strikeOutPosition) {
		default:
			break;

		case 1:
			getScreen()->draw(getWorld()->graphicResourceIds[5], _frameIndex, Common::Point(38, 345));
			break;

		case 2:
			getScreen()->draw(getWorld()->graphicResourceIds[5], _frameIndex, Common::Point(17, 226));
			break;

		case 3:
			getScreen()->draw(getWorld()->graphicResourceIds[5], _frameIndex, Common::Point(1, 104));
			break;

		case 4:
			getScreen()->draw(getWorld()->graphicResourceIds[6], _frameIndex, Common::Point(43, 117));
			break;

		case 5:
			getScreen()->draw(getWorld()->graphicResourceIds[6], _frameIndex, Common::Point(176, 104));
			break;

		case 6:
			getScreen()->draw(getWorld()->graphicResourceIds[6], _frameIndex, Common::Point(299, 85));
			break;

		case 7:
			if (_counter2 == 0)
				getScreen()->draw(getWorld()->graphicResourceIds[8], _frameIndex, Common::Point(30, 149));
			else if (_counter2 == 1)
				getScreen()->draw(getWorld()->graphicResourceIds[8], _frameIndex, Common::Point(180, 249));
			else {
				getScreen()->draw(getWorld()->graphicResourceIds[8], 6,  Common::Point(30, 149));
				getScreen()->draw(getWorld()->graphicResourceIds[8], 6, Common::Point(180, 249));
				getScreen()->draw(getWorld()->graphicResourceIds[8], _frameIndex, Common::Point(330, 349));
			}
			break;

		case 8:
			if (_counter2 == 0)
				getScreen()->draw(getWorld()->graphicResourceIds[10], _frameIndex, Common::Point(69, 66));
			else if (_counter2 == 1)
				getScreen()->draw(getWorld()->graphicResourceIds[10], _frameIndex, Common::Point(-22, 220));
			else {
				getScreen()->draw(getWorld()->graphicResourceIds[10], 6, Common::Point( 69, 66));
				getScreen()->draw(getWorld()->graphicResourceIds[10], 6, Common::Point(-22, 220));
				getScreen()->draw(getWorld()->graphicResourceIds[10], _frameIndex, Common::Point(-110, 370));
			}
			break;

		case 9:
			getScreen()->draw(getWorld()->graphicResourceIds[4], _frameIndex, Common::Point(38, 345));
			break;

		case 10:
			getScreen()->draw(getWorld()->graphicResourceIds[4], _frameIndex, Common::Point(17, 226));
			break;

		case 11:
			getScreen()->draw(getWorld()->graphicResourceIds[4], _frameIndex, Common::Point(1, 104));
			break;

		case 12:
			getScreen()->draw(getWorld()->graphicResourceIds[11], _frameIndex, Common::Point(43, 117));
			break;

		case 13:
			getScreen()->draw(getWorld()->graphicResourceIds[11], _frameIndex, Common::Point(176, 104));
			break;

		case 14:
			getScreen()->draw(getWorld()->graphicResourceIds[11], _frameIndex, Common::Point(299, 85));
			break;

		case 15:
			if (_counter2 == 0)
				getScreen()->draw(getWorld()->graphicResourceIds[7], _frameIndex, Common::Point(30, 149));
			else if (_counter2 == 1)
				getScreen()->draw(getWorld()->graphicResourceIds[7], _frameIndex, Common::Point(180, 249));
			else {
				getScreen()->draw(getWorld()->graphicResourceIds[7], 6, Common::Point(30, 149));
				getScreen()->draw(getWorld()->graphicResourceIds[7], 6, Common::Point(180, 249));
				getScreen()->draw(getWorld()->graphicResourceIds[7], _frameIndex, Common::Point(330, 349));
			}
			break;

		case 16:
			if (_counter2 == 0)
				getScreen()->draw(getWorld()->graphicResourceIds[9], _frameIndex, Common::Point(69, 66));
			else if (_counter2 == 1)
				getScreen()->draw(getWorld()->graphicResourceIds[9], _frameIndex, Common::Point(-22, 220));
			else {
				getScreen()->draw(getWorld()->graphicResourceIds[9], 6, Common::Point(69, 66));
				getScreen()->draw(getWorld()->graphicResourceIds[9], 6, Common::Point(-22, 220));
				getScreen()->draw(getWorld()->graphicResourceIds[9], _frameIndex, Common::Point(-110, 370));
			}
			break;
		}

		if (_frameIndex >= _frameCount) {
			if (_strikeOutPosition == 7 || _strikeOutPosition == 8 || _strikeOutPosition == 15 || _strikeOutPosition == 16) {
				if (_counter2 < 2) {
					_frameIndex = 0;
					++_counter2;
				}
			}
		} else {
			++_frameIndex;
		}

		if (!_counter)
			_counter = 30;
	}

	getScreen()->draw(getWorld()->graphicResourceIds[17], 0, Common::Point(0, 0));
}

void PuzzleTicTacToe::updatePositions(uint32 field1, uint32 field2, uint32 field3) {
	if (_gameField[field1] != ' ') {
		_field[_emptyCount] = field3;
		_field[_emptyCount + 1] = field2;

		_emptyCount += 2;
	}

	if (_gameField[field3] != ' ') {
		_field[_emptyCount] = field1;
		_field[_emptyCount + 1] = field2;

		_emptyCount += 2;
	}

	if (_gameField[field2] != ' ') {
		_field[_emptyCount] = field3;
		_field[_emptyCount + 1] = field1;

		_emptyCount += 2;
	}
}

//////////////////////////////////////////////////////////////////////////
// Game
//////////////////////////////////////////////////////////////////////////
bool PuzzleTicTacToe::check() {
	if (_needToInitialize)
		return false;

	if (!checkWinning('X')
	 && !checkWinning('O')
	 && !checkFieldsUpdatePositions()
	 && !checkFields()
	 && !countEmptyFields()) {
		if (!_counter)
			getCursor()->show();

		_needToInitialize = true;

		return false;
	}

	return true;
}

PuzzleTicTacToe::GameStatus PuzzleTicTacToe::checkField(uint32 field1, uint32 field2, uint32 field3, char mark, uint32 *counterX, uint32 *counterO) const {
	*counterX = 0;
	*counterO = 0;
	GameStatus status = kStatus0;

	if (_gameField[field1] == 'X')
		++*counterX;
	if (_gameField[field2] == 'X')
		++*counterX;
	if (_gameField[field3] == 'X')
		++*counterX;

	if (_gameField[field1] == 'O')
		++*counterO;
	if (_gameField[field2] == 'O')
		++*counterO;
	if (_gameField[field3] == 'O')
		++*counterO;

	if (mark == 'O') {
		if (*counterO == 1 && !*counterX)
			status = kStatusFree;
	} else if (mark == 'X') {
		if (!*counterO && *counterX == 1)
			status = kStatusFree;
	}

	if (mark == 'O') {
		if (!*counterO && *counterX == 2)
			status = kStatusNeedBlocking;
	} else if (mark == 'X') {
		if (*counterO == 2 && !*counterX)
			status = kStatusNeedBlocking;
	}

	return status;
}

bool PuzzleTicTacToe::checkFieldsUpdatePositions() {
	uint32 counterX = 0;
	uint32 counterO = 0;

	for (uint32 i = 0; i < ARRAYSIZE(puzzleTicTacToeFieldsToCheck); i++)
		if (checkField(puzzleTicTacToeFieldsToCheck[i].field1, puzzleTicTacToeFieldsToCheck[i].field2, puzzleTicTacToeFieldsToCheck[i].field3, 'O', &counterX, &counterO) == kStatusFree)
			updatePositions(puzzleTicTacToeFieldsToCheck[i].field1, puzzleTicTacToeFieldsToCheck[i].field2, puzzleTicTacToeFieldsToCheck[i].field3);

	return (_emptyCount != 0);
}

bool PuzzleTicTacToe::checkFields() {
	uint32 counterX = 0;
	uint32 counterO = 0;

	for (uint32 i = 0; i < ARRAYSIZE(puzzleTicTacToeFieldsToCheck); i++) {
		checkField(puzzleTicTacToeFieldsToCheck[i].field1, puzzleTicTacToeFieldsToCheck[i].field2, puzzleTicTacToeFieldsToCheck[i].field3, 'O', &counterX, &counterO);

		if (counterX || counterO)
			continue;

		_field[_emptyCount]     = puzzleTicTacToeFieldsToCheck[i].field1;
		_field[_emptyCount + 1] = puzzleTicTacToeFieldsToCheck[i].field2;
		_field[_emptyCount + 2] = puzzleTicTacToeFieldsToCheck[i].field3;

		_emptyCount += 3;
	}

	return (_emptyCount != 0);
}

uint32 PuzzleTicTacToe::checkPosition(uint32 position1, uint32 position2, uint position3) const {
	if (_gameField[position1] == ' ')
		return position1;

	if (_gameField[position2] == ' ')
		return position2;

	return position3;
}

bool PuzzleTicTacToe::checkWinner() {
	if (_needToInitialize)
		return true;

	if (checkWinnerHelper() == 1) {
		_vm->setGameFlag(kGameFlag114);
		_ticker = 30;
		return true;
	}

	if (checkWinnerHelper() == -1) {
		_vm->setGameFlag(kGameFlag215);
		_ticker = 30;
		return true;
	}

	return false;
}

int32 PuzzleTicTacToe::checkWinnerHelper() {
	uint32 counterX = 0;
	uint32 counterO = 0;

	for (uint32 i = 0; i < ARRAYSIZE(puzzleTicTacToeFieldsToCheck); i++) {
		checkField(puzzleTicTacToeFieldsToCheck[i].field1, puzzleTicTacToeFieldsToCheck[i].field2, puzzleTicTacToeFieldsToCheck[i].field3, 'O', &counterX, &counterO);

		if (counterX == 3) {
			_strikeOutPosition = puzzleTicTacToeFieldsToCheck[i].strikeOutPositionX;
			_frameCount = puzzleTicTacToeFieldsToCheck[i].frameCount;
			_frameIndex = 0;
			return 1;
		}

		if (counterO == 3) {
			_strikeOutPosition = puzzleTicTacToeFieldsToCheck[i].strikeOutPositionO;
			_frameCount = puzzleTicTacToeFieldsToCheck[i].frameCount;
			_frameIndex = 0;
			return -1;
		}
	}

	return 0;
}

bool PuzzleTicTacToe::checkWinning(char mark) {
	uint32 counterX = 0;
	uint32 counterO = 0;
	_emptyCount = 0;

	for (uint32 i = 0; i < ARRAYSIZE(puzzleTicTacToeFieldsToCheck); i++) {
		if (checkField(puzzleTicTacToeFieldsToCheck[i].field1, puzzleTicTacToeFieldsToCheck[i].field2, puzzleTicTacToeFieldsToCheck[i].field3, mark, &counterX, &counterO) == kStatusNeedBlocking) {
			_field[_emptyCount] = checkPosition(puzzleTicTacToeFieldsToCheck[i].field1, puzzleTicTacToeFieldsToCheck[i].field2, puzzleTicTacToeFieldsToCheck[i].field3);
			++_emptyCount;
		}
	}

	return (_emptyCount != 0);
}

bool PuzzleTicTacToe::countEmptyFields() {
	_emptyCount = 0;

	for (uint32 i = 0; i < ARRAYSIZE(_gameField); i++) {
		if (_gameField[i] == ' ') {
			_field[i] = i;
			++_emptyCount;
		}
	}

	return (_emptyCount != 0);
}

void PuzzleTicTacToe::placeOpponentMark() {
	_frameIndex = 0;
	_lastMarkedField = _field[rnd(_emptyCount)];

	if (_gameField[_lastMarkedField] != ' ')
		error("[PuzzleTicTacToe::placeOpponentMark] Field is already occupied (%d)!", _lastMarkedField);

	_gameField[_lastMarkedField] = 'O';

	getSound()->playSound(getWorld()->soundResourceIds[12], false, Config.sfxVolume - 100);
}

} // End of namespace Asylum
