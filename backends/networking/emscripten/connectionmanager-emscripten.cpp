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

#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "backends/networking/emscripten/connectionmanager-emscripten.h"
#include "backends/networking/emscripten/networkreadstream-emscripten.h"
#include "backends/networking/emscripten/slist.h"
#include "common/debug.h"
#include "common/fs.h"
#include "common/system.h"
#include "common/timer.h"

namespace Common {

DECLARE_SINGLETON(Networking::ConnectionManagerEmscripten);

}

namespace Networking {

ConnectionManagerEmscripten::~ConnectionManagerEmscripten() {
	stopTimer();

	// terminate all requests
	_handleMutex.lock();
	for (Common::Array<RequestWithCallback>::iterator i = _requests.begin(); i != _requests.end(); ++i) {
		Request *request = i->request;
		RequestCallback callback = i->onDeleteCallback;
		if (request)
			request->finish();
		delete request;
		if (callback)
			(*callback)(request);
	}
	_requests.clear();
}

Request *ConnectionManagerEmscripten::addRequest(Request *request, RequestCallback callback) {

	_addedRequestsMutex.lock();
	_addedRequests.push_back(RequestWithCallback(request, callback));

	if (!_timerStarted) {
		if (g_system->getTimerManager()) {
			startTimer();
		} else {
			request->handle(); // kick it off manually if there's no timer manager yet
		}
	}
	_addedRequestsMutex.unlock();
	return request;
}
Common::String ConnectionManagerEmscripten::urlEncode(Common::String s) const {
	return s.c_str(); // TODO: We need to actually escape this. Maybe like this:
	/*
	int x = EM_ASM_INT({
  console.log('I received: ' + $0);
  return $0 + 1;
}, 100);

but using https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/encodeURIComponent

	*/
}

uint32 ConnectionManagerEmscripten::getCloudRequestsPeriodInMicroseconds() {
	return TIMER_INTERVAL * CLOUD_PERIOD;
}

// private goes here:

void connectionsThread(void *ignored) {
	ConnMan.handle();
}

void ConnectionManagerEmscripten::startTimer(int interval) {
	Common::TimerManager *manager = g_system->getTimerManager();
	if (manager) {
		if (manager->installTimerProc(connectionsThread, interval, nullptr, "Networking::ConnectionManagerEmscripten's Timer")) {
			_timerStarted = true;
		} else {
			error("ConnectionManagerEmscripten::startTimer - Failed to install timer");
		}
	} else {

		error("ConnectionManagerEmscripten::startTimer - TimerManager not yet available");
	}
}

void ConnectionManagerEmscripten::stopTimer() {
	debug(9, "timer stopped");
	Common::TimerManager *manager = g_system->getTimerManager();
	manager->removeTimerProc(connectionsThread);
	_timerStarted = false;
}

bool ConnectionManagerEmscripten::hasAddedRequests() {
	_addedRequestsMutex.lock();
	bool hasNewRequests = !_addedRequests.empty();
	_addedRequestsMutex.unlock();
	return hasNewRequests;
}

void ConnectionManagerEmscripten::handle() {
	// lock mutex here (in case another handle() would be called before this one ends)
	_handleMutex.lock();
	++_frame;
	if (_frame % CLOUD_PERIOD == 0)
		interateRequests();
	if (_frame % CURL_PERIOD == 0)
		processTransfers();

	if (_requests.empty() && !hasAddedRequests() && _timerStarted)
		stopTimer();
	_handleMutex.unlock();
}

void ConnectionManagerEmscripten::interateRequests() {
	// add new requests
	_addedRequestsMutex.lock();
	for (Common::Array<RequestWithCallback>::iterator i = _addedRequests.begin(); i != _addedRequests.end(); ++i) {
		_requests.push_back(*i);
	}
	_addedRequests.clear();
	_addedRequestsMutex.unlock();

	for (Common::Array<RequestWithCallback>::iterator i = _requests.begin(); i != _requests.end();) {

		Request *request = i->request;
		if (request) {
			if (request->state() == PROCESSING)
				request->handle();
			else if (request->state() == RETRY)
				request->handleRetry();
		}

		if (!request || request->state() == FINISHED) {
			delete (i->request);
			if (i->onDeleteCallback) {
				(*i->onDeleteCallback)(i->request); // that's not a mistake (we're passing an address and that method knows there is no object anymore)
			}
			_requests.erase(i);
			continue;
		}

		++i;
	}
}

void ConnectionManagerEmscripten::processTransfers() {
	// done by the stream themselves
	return;
}

} // End of namespace Networking