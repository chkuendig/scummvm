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

#include "backends/text-to-speech/emscripten/emscripten-text-to-speech.h"

#if defined(USE_TTS) && defined(EMSCRIPTEN)
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/system.h"
#include "common/translation.h"
#include "common/ustr.h"

EmscriptenTextToSpeechManager::EmscriptenTextToSpeechManager() {
	ttsInit();
#ifdef USE_TRANSLATION
	setLanguage(TransMan.getCurrentLanguage());
#else
	setLanguage("en");
#endif
}

EmscriptenTextToSpeechManager::~EmscriptenTextToSpeechManager() {
	stop();
}

bool EmscriptenTextToSpeechManager::say(const Common::U32String &str, Action action) {
	assert(_ttsState->_enabled);

	Common::String strUtf8 = str.encode();
	debug(5, "Saying %s (%d)", strUtf8.c_str(), action);

	if (isSpeaking() && action == DROP) {
		debug(5, "EmscriptenTextToSpeechManager::say - Not saying '%s' as action=DROP and already speaking", strUtf8.c_str());
		return true;
	}

	char *voice_name = ((char **)_ttsState->_availableVoices[_ttsState->_activeVoice].getData())[0];
	char *voice_lang = ((char **)_ttsState->_availableVoices[_ttsState->_activeVoice].getData())[1];
	return ttsSay(strUtf8.c_str(), voice_name, voice_lang, _ttsState->_pitch, _ttsState->_rate, _ttsState->_volume, action);
}

void EmscriptenTextToSpeechManager::updateVoices() {
	_ttsState->_availableVoices.clear();
	char **ttsVoices = ttsGetVoices();
	char **iter = ttsVoices;
	Common::Array<char *> names;
	while (strcmp(*iter, "") != 0) {
		char *c_name = *iter++;
		char *c_lang = *iter++;
		Common::String language = Common::String(c_lang);
		if (_ttsState->_language == language.substr(0, 2)) {
			int idx = -1;
			for (int i = 0; i < names.size(); i++) {
				if (strcmp(names[i], c_name) == 0) {
					idx = i;
					break;
				}
			}
			names.push_back(c_name);
			Common::String name;
			// some systems have the same voice multiple times for the same language (e.g. en-US and en-GB),
			// in that case we should add the locale to the name
			if (idx == -1) {
				name = Common::String(c_name);
			} else {
				name = Common::String::format("%s (%s)", c_name, language.substr(3, 2).c_str());
				// some systems have identical name/language/locale pairs multiple times (seems a bug), we just skip that case (e.g. macOS Safari for "Samantha (en_US)" )
				char *other_name = ((char **)_ttsState->_availableVoices[idx].getData())[0];
				char *other_lang = ((char **)_ttsState->_availableVoices[idx].getData())[1];
				Common::String other_new = Common::String::format("%s (%s)", other_name, Common::String(other_lang).substr(3, 2).c_str());
				if (other_new == name) {
					warning("Skipping duplicate voice %s %s", c_name, c_lang);
					continue;
				} else {
					warning("Adding duplicate voice %s %s", _ttsState->_availableVoices[idx].getDescription().c_str(), name.c_str());
					_ttsState->_availableVoices[idx].setDescription(other_new);
				}
			}
			char **data_p = new char *[] { c_name, c_lang };
			Common::TTSVoice voice(Common::TTSVoice::UNKNOWN_GENDER, Common::TTSVoice::UNKNOWN_AGE, (void *)data_p, name);
			_ttsState->_availableVoices.push_back(voice);
		}
	}
	free(ttsVoices);

	if (_ttsState->_availableVoices.empty()) {
		warning("No voice is available for language: %s", _ttsState->_language.c_str());
	}
}

bool EmscriptenTextToSpeechManager::stop() {
	ttsStop();
	return true;
}

bool EmscriptenTextToSpeechManager::pause() {
	if (isPaused())
		return false;
	ttsPause();
	return true;
}

bool EmscriptenTextToSpeechManager::resume() {
	if (!isPaused())
		return false;
	ttsResume();
	return true;
}

bool EmscriptenTextToSpeechManager::isSpeaking() {
	return ttsIsSpeaking();
}

bool EmscriptenTextToSpeechManager::isPaused() {
	return ttsIsPaused();
}

bool EmscriptenTextToSpeechManager::isReady() {
	if (_ttsState->_availableVoices.empty())
		return false;
	if (!isPaused() && !isSpeaking())
		return true;
	else
		return false;
}

void EmscriptenTextToSpeechManager::setVoice(unsigned index) {
	assert(!_ttsState->_enabled || index < _ttsState->_availableVoices.size());
	_ttsState->_activeVoice = index;
	return;
}

void EmscriptenTextToSpeechManager::setRate(int rate) {
	assert(rate >= -100 && rate <= 100);
	_ttsState->_rate = rate;
}

void EmscriptenTextToSpeechManager::setPitch(int pitch) {
	assert(pitch >= -100 && pitch <= 100);
	_ttsState->_pitch = pitch;
}

void EmscriptenTextToSpeechManager::setVolume(unsigned volume) {
	assert(volume <= 100);
	_ttsState->_volume = volume;
}

void EmscriptenTextToSpeechManager::setLanguage(Common::String language) {
	debug(5, "EmscriptenTextToSpeechManager::setLanguage to %s", language.c_str());
	if (_ttsState->_language != language.substr(0, 2) || _ttsState->_availableVoices.empty()) {
		debug(5, "EmscriptenTextToSpeechManager::setLanguage - Update voices");
		updateVoices();
		setVoice(0);
	}
	Common::TextToSpeechManager::setLanguage(language);
}

void EmscriptenTextToSpeechManager::freeVoiceData(void *data) {
	free(((char **)data)[0]);
	free(((char **)data)[1]);
	free(data);
}

#endif
