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
 * This file implements WebMIDI C API functions in JavaScript for ScummVM's Emscripten port.
 * See: https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html#implement-a-c-api-in-javascript
 */

mergeInto(LibraryManager.library, {
    /**
     * Open a MIDI output device
     * @param {number} id_c - pointer to device ID string in WASM memory
     * @returns {Promise<number>} 0 on success, 1 on error
     */
    midiOpen__async: true,
    midiOpen:  (id_c) => Asyncify.handleSleep(async (wakeUp) => {
        var id = UTF8ToString(id_c);
        try {
            await globalThis.midiOutputMap.get(id).open();
            wakeUp(0);
        } catch (error) {
            console.warn(error);
            wakeUp(1);
        }
    }),

    /**
     * Check if a MIDI output device is open
     * @param {number} id_c - pointer to device ID string in WASM memory
     * @returns {boolean} true if device is open
     */
    midiIsOpen: function(id_c) {
        var id = UTF8ToString(id_c);
        return globalThis.midiOutputMap.get(id) && globalThis.midiOutputMap.get(id).connection == "open";
    },

    /**
     * Close a MIDI output device
     * @param {number} id_c - pointer to device ID string in WASM memory
     */
    midiClose__async: true,
    midiClose: (id_c) => Asyncify.handleSleep(async (wakeUp) => {
        var id = UTF8ToString(id_c);
        await globalThis.midiOutputMap.get(id).close();
        wakeUp();
    }),

    /**
     * Send a MIDI message to an output device
     * @param {number} id_c - pointer to device ID string in WASM memory
     * @param {number} status_byte - MIDI status byte
     * @param {number} first_byte - first data byte
     * @param {number} second_byte - second data byte
     */
    midiSendMessage: function(id_c, status_byte, first_byte, second_byte) {
        var id = UTF8ToString(id_c);
        if (status_byte < 0xc0 || status_byte > 0xdf) {
            globalThis.midiOutputMap.get(id).send([status_byte, first_byte, second_byte]);
        } else {
            globalThis.midiOutputMap.get(id).send([status_byte, first_byte]);
        }
    },

    /**
     * Get an array of available MIDI output device names
     * @returns {number} pointer to array of C strings
     */
    midiGetOutputNames__deps: ['$lengthBytesUTF8','$stringToUTF8Array','malloc','$setValue'],
    midiGetOutputNames__async: true,
	midiGetOutputNames: () => Asyncify.handleSleep(async (wakeUp) => {
        if (!globalThis.midiOutputMap) {
            if (!("requestMIDIAccess" in navigator)) {
                console.warn("midiGetOutputNames: Browser does not support Web MIDI API.");
                globalThis.midiOutputMap = [];
            } else {
                await navigator
                    .requestMIDIAccess({ sysex: true, software: true })
                    .then((midiAccess) => {
                        globalThis.midiOutputMap = midiAccess.outputs;
                        midiAccess.onstatechange = (e) => {
                            console.debug("midiGetOutputNames: MIDI device list changed.");
                            globalThis.midiOutputMap = e.target.outputs;
                        };
                    })
                    .catch((error) => {
                        console.warn("midiGetOutputNames: Error accessing MIDI devices:", error);
                        globalThis.midiOutputMap = [];
                    });
            }
        }
        deviceNames = Array.from(globalThis.midiOutputMap).map( (elem) => elem[1].name);
        deviceNames.push(""); // we need this to find the end of the array on the native side.

        // convert the strings to C strings
        var c_strings = deviceNames.map((s) => {
            var size = lengthBytesUTF8(s) + 1;
            var ret = _malloc(size);
            stringToUTF8Array(s, HEAP8, ret, size);
            return ret;
        });

        // populate return array
        var ret_arr = _malloc(c_strings.length * 4); // 4-bytes per pointer
        c_strings.forEach((ptr, i) => { setValue(ret_arr + i * 4, ptr, "i32"); });

        wakeUp(ret_arr);
    }),

    /**
     * Get the device ID for a MIDI output device by name
     * @param {number} name_c - pointer to device name string in WASM memory
     * @returns {number} pointer to device ID string
     */
    midiGetOutputId: function(name_c) {
        var name = UTF8ToString(name_c);
        for (const [key, midiOutput] of globalThis.midiOutputMap.entries()) {
            if (midiOutput.name == name) {
                return stringToNewUTF8(key);
            }
        }
        return;
    }
});
