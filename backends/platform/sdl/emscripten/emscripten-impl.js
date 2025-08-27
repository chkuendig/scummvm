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
     * Check if the browser is currently in fullscreen mode
     * @returns {boolean} true if in fullscreen mode, false otherwise
     */
    isFullscreen: function() {
        return !!document.fullscreenElement;
    },

    /**
     * Toggle fullscreen mode for the canvas element
     * @param {boolean} enable - true to enter fullscreen, false to exit
     */
    toggleFullscreen: function(enable) {
        let canvas = document.getElementById('canvas');
        if (enable && !document.fullscreenElement) {
            canvas.requestFullscreen();
        }
        if (!enable && document.fullscreenElement) {
            document.exitFullscreen();
        }
    },

    /**
     * Download a file by creating a blob and triggering a download
     * @param {number} filenamePtr - pointer to filename string in WASM memory
     * @param {number} dataPtr - pointer to file data in WASM memory
     * @param {number} dataSize - size of the file data in bytes
     */
    downloadFile: function(filenamePtr, dataPtr, dataSize) {
        const view = new Uint8Array(HEAPU8.buffer, dataPtr, dataSize);
        const blob = new Blob([view], {
            type: 'octet/stream'
        });
        const filename = UTF8ToString(filenamePtr);
        setTimeout(() => {
            const a = document.createElement('a');
            a.style = 'display:none';
            document.body.appendChild(a);
            const url = window.URL.createObjectURL(blob);
            a.href = url;
            a.download = filename;
            a.click();
            window.URL.revokeObjectURL(url);
            document.body.removeChild(a);
        }, 0);
    },

    /**
     * Open OAuth window for cloud connection and listen for response
     * @param {number} urlPtr - pointer to URL string in WASM memory
     * @returns {boolean} true if window was opened successfully
     */
    cloudConnectionWizardOAuthWindow__deps: ['$ccall'],
    cloudConnectionWizardOAuthWindow: function(urlPtr) {
        const url = UTF8ToString(urlPtr);
        const oauth_window = window.open(url);
        
        window.addEventListener("message", (event) => {
            // Call the C++ callback function with the JSON data
            ccall('cloudConnectionWizardCallback', null, ['string'], [JSON.stringify(event.data)]);
            oauth_window.close();
        }, {once: true});
        
        return true;
    },



});
