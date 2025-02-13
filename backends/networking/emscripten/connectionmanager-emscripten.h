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

#ifndef BACKENDS_NETWORKING_EMSCRIPTEN_CONNECTIONMANAGER_H
#define BACKENDS_NETWORKING_EMSCRIPTEN_CONNECTIONMANAGER_H

#include "backends/networking/curl/connectionmanager.h"

namespace Networking {

//class NetworkReadStreamEmscripten;
class ConnectionManagerEmscripten : public ConnectionManager, public Common::Singleton<ConnectionManagerEmscripten> {

	friend void connectionsThread(void *); //calls handle()
	void startTimer(int interval = TIMER_INTERVAL);
	void stopTimer();
	void handle();
	void interateRequests();
	void processTransfers();
	bool hasAddedRequests();

	
public:

	virtual ~ConnectionManagerEmscripten();
	/**
	 * Use this method to add new Request into manager's queue.
	 * Manager will periodically call handle() method of these
	 * Requests until they set their state to FINISHED.
	 * // TODO: Figure out why this simple logic (check if finished) doesn't work in readstream
	 * If Request's state is RETRY, handleRetry() is called instead.
	 *
	 * The passed callback would be called after Request is deleted.
	 *
	 * @return the same Request pointer, just as a shortcut
	 */
	Request *addRequest(Request *request, RequestCallback callback = nullptr);

	Common::String urlEncode(Common::String s) const;

	static uint32 getCloudRequestsPeriodInMicroseconds();
	bool timerStarted(){return _timerStarted;}
};

} // End of namespace Networking

#define ConnMan     Networking::ConnectionManagerEmscripten::instance()
#endif
