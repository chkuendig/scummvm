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

/**
 * This file implements C API functions in JavaScript for ScummVM's Emscripten port.
 * See: https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html#implement-a-c-api-in-javascript
 */

mergeInto(LibraryManager.library, {
  
    /**
     * Open OAuth window for cloud connection and listen for response
     * @param {number} urlPtr - pointer to URL string in WASM memory
     * @returns {boolean} true if window was opened successfully
     */
    OSystem_Emscripten_openCloudOAuthWindow__deps: ['$ccall'],
    OSystem_Emscripten_openCloudOAuthWindow: function(urlPtr) {
        const url = UTF8ToString(urlPtr);
        const oAuthWindow = window.open(url);
        
        const messageHandler = function(event) {
            if(event.origin == "https://cloud.scummvm.org") { // Ensure message is from trusted origin
            // Call the C++ callback function with the JSON data
            ccall('OSystem_Emscripten_cloudConnectionWizardCallback', null, ['string'], [JSON.stringify(event.data)]);
            oAuthWindow.close();
            window.removeEventListener("message", messageHandler);
            }
        };
        window.addEventListener("message", messageHandler);
        
        return true;
    },



});
