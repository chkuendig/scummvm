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
 * This file implements Text-to-Speech C API functions in JavaScript for ScummVM's Emscripten port.
 * See: https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html#implement-a-c-api-in-javascript
 */

mergeInto(LibraryManager.library, {
    /**
     * Initialize the text-to-speech system
     */
    ttsInit: function() {
        /* 
        * Voices can come from the browser, the operating system or cloud services. This means we sometimes get
        * an incomplete or empty list on first call getVoices().
        * Best practice is to listen to the 'voiceschanged' event and update the list of voices when it fires.
        */
        globalThis.ttsVoiceMap = {};
        globalThis.ttsUtteranceQueue = [];
        const refreshVoices = () => {
            globalThis.ttsVoiceMap = {};
            var cnt = 0;
            voices = window.speechSynthesis.getVoices();
            Array.from(voices).forEach((voice) => {
                if (!(voice.lang in globalThis.ttsVoiceMap)) {
                    globalThis.ttsVoiceMap[voice.lang] = {};
                }
                globalThis.ttsVoiceMap[voice.lang][voice.name] = voice;
                cnt++;
            });
            console.debug("Found %d voices",cnt);
        };

        if ('onvoiceschanged' in speechSynthesis) {
            speechSynthesis.onvoiceschanged = refreshVoices;
        } 
        refreshVoices();
    },

    /**
     * Speak text using text-to-speech
     * @param {number} text - pointer to text string in WASM memory
     * @param {number} voice_name - pointer to voice name string in WASM memory
     * @param {number} voice_lang - pointer to voice language string in WASM memory
     * @param {number} pitch - pitch value (-100 to 100)
     * @param {number} rate - rate value (-100 to 100)
     * @param {number} volume - volume value (0 to 100)
     * @param {number} action - action type (0=INTERRUPT, 1=INTERRUPT_NO_REPEAT, 2=QUEUE, 3=QUEUE_NO_REPEAT)
     * @returns {boolean} true if successful, false otherwise
     */
    ttsSay: function(text, voice_name, voice_lang, pitch, rate, volume, action) {
        voice_name = UTF8ToString(voice_name);
        voice_lang = UTF8ToString(voice_lang);
        if (!(voice_lang in globalThis.ttsVoiceMap && voice_name in globalThis.ttsVoiceMap[voice_lang])){
            console.error("ttsSay: Voice not found");
            return false;
        }
        text = UTF8ToString(text);
        if (text === "") {
            return false;
        }
        /* 
        * Possible actions are:
        *    INTERRUPT - interrupts the current speech
        *    INTERRUPT_NO_REPEAT - interrupts the speech (deletes the whole queue),
        *        if the str is the same as the string currently being said,
        *        it lets the current string finish.
        *    QUEUE - queues the speech
        *    QUEUE_NO_REPEAT - queues the speech only if the str is different than
        *        the last string in the queue (or the string, that is currently
        *        being said if the queue is empty)
        *    DROP - does nothing if there is anything being said at the moment
        */
        const Actions = Object.freeze({
            INTERRUPT: 0,
            INTERRUPT_NO_REPEAT: 1,
            QUEUE: 2,
            QUEUE_NO_REPEAT: 3
        });
        console.assert(action <= 3,"ttsSay: Illegal Action: %d",action);// DROP is handled on the native side so we should only have 0-3.

        if (action == Actions.QUEUE_NO_REPEAT && 
                globalThis.ttsUtteranceQueue.length > 0 && globalThis.ttsUtteranceQueue[globalThis.ttsUtteranceQueue.length-1].text == text) {
            console.debug("ttsSay: Skipping duplicate utterance (QUEUE_NO_REPEAT)");
            return false;
        }
        //  INTERRUPT_NO_REPEAT with a matching string - empty queue but let the current string finish
        if (action == Actions.INTERRUPT_NO_REPEAT && globalThis.ttsUtteranceQueue.length > 0 && globalThis.ttsUtteranceQueue[0].text == text){
            globalThis.ttsUtteranceQueue = globalThis.ttsUtteranceQueue.slice(0,1);
            return false;
        }
        // interrupt or INTERRUPT_NO_REPEAT with a non-matching string (or no string talking) - empty queue, cancel all talking
        if (action == Actions.INTERRUPT || action == Actions.INTERRUPT_NO_REPEAT ) {
            globalThis.ttsUtteranceQueue = [];//globalThis.ttsUtteranceQueue.slice(0,1);
            window.speechSynthesis.cancel();
            
        }
        // queue and speak next utterance
        voice = globalThis.ttsVoiceMap[voice_lang][voice_name];
        const utterance = new SpeechSynthesisUtterance(text);
        utterance.onend = function(event) { // this is triggered when an utterance completes speaking 
            if (globalThis.ttsUtteranceQueue[0] == event.target){
                globalThis.ttsUtteranceQueue.shift(); //remove utterance that was just spoken
            }
            if (globalThis.ttsUtteranceQueue.length > 0 && !window.speechSynthesis.speaking){ // speak next utterance if nothing is being spoken
                window.speechSynthesis.speak(globalThis.ttsUtteranceQueue[0]);
            }
        };
        utterance.onerror = function(event) { // this includes canceled utterances (so not just errors)
            if (globalThis.ttsUtteranceQueue[0] == event.target){
                globalThis.ttsUtteranceQueue.shift(); //remove utterance that was just spoken
            }
            if (globalThis.ttsUtteranceQueue.length > 0 && !window.speechSynthesis.speaking){ // speak next utterance if nothing is being spoken
                window.speechSynthesis.speak(globalThis.ttsUtteranceQueue[0]);
            }
        };
        /* 
         * TODO: we could do INTERRUPT_NO_REPEAT and INTERRUPT handling on boundaries, but it's not reliable
         *       remote voices don't have onboundary event: https://issues.chromium.org/issues/41195426
         * 
         *     utterance.onboundary = function(event){
         *        console.log(event);
         *    };
        */
        utterance.voice = voice;
        utterance.volume = volume / 100; // linearly adjust 0 to 100 -> 0 to 1
        utterance.pitch = (pitch + 100) / 100; // linearly adjust -100 to 100 (0 default) -> 0 to 2 (1 default)
        utterance.rate = rate > 0 ? 1 + (rate / (100 - 9)) : 0.1 + (rate + 100) / (100 / 0.9); // linearly adjust -100 to 100 (0 default)  -> 0.1 to 10 (1 default)
        
        console.debug("Pushing to queue: %s",text);
        globalThis.ttsUtteranceQueue.push(utterance);
        if (globalThis.ttsUtteranceQueue.length == 1){
            console.debug("Speaking %s",text);
            window.speechSynthesis.speak(utterance);
        }
        return true;
    },

    /**
     * Get available text-to-speech voices
     * @returns {number} pointer to array of C strings
     */

    ttsGetVoices__deps: ['malloc','$setValue','$lengthBytesUTF8','$stringToUTF8Array'],
    ttsGetVoices: function() {
        voices = Array.from(Object.values(globalThis.ttsVoiceMap)).map(Object.values).flat() // flatten voice map
            .sort((a,b) => a.default ===  b.default ? a.name.localeCompare(b.name):a.default?-1:1) // first default, then alphabetically
            .map(voice=>[voice.name,voice.lang])
            .flat();
        voices.push(""); // we need this to find the end of the array on the native side.

        // convert the strings to C strings
        var c_strings = voices.map((s) => {
            var size = lengthBytesUTF8(s) + 1;
            var ret = _malloc(size);
            stringToUTF8Array(s, HEAP8, ret, size);
            return ret;
        });

        var ret_arr = _malloc(c_strings.length * 4); // 4-bytes per pointer
        c_strings.forEach((ptr, i) => { setValue(ret_arr + i * 4, ptr, "i32"); }); // populate return array
        return ret_arr;
    },

    /**
     * Stop text-to-speech
     */
    ttsStop: function() {
        window.speechSynthesis.cancel();
    },

    /**
     * Pause text-to-speech (async)
     */

    ttsPause__async: true,
    ttsPause: () => Asyncify.handleSleep(async (wakeUp) => {
        if(window.speechSynthesis.paused){
        } else if(window.speechSynthesis.speaking && globalThis.ttsUtteranceQueue.length > 0){
            // browsers don't pause immediately, so we have to wait for the pause event if there's something being spoken
            await (async () => {
                return new Promise((resolve, reject) => {
                    setTimeout(() => { resolve(); }, 300);
                    globalThis.ttsUtteranceQueue[0].onpause = (event) =>{ resolve(event)};
                    window.speechSynthesis.pause();
                });
            })();
        } else {
            console.assert(globalThis.ttsUtteranceQueue.length == 0);
            window.speechSynthesis.pause();
        }
        wakeUp();
    }),

    /**
     * Resume text-to-speech
     */
    ttsResume: function() {
        window.speechSynthesis.resume();
    },

    /**
     * Check if text-to-speech is currently speaking
     * @returns {boolean} true if speaking, false otherwise
     */
    ttsIsSpeaking: function() {
        return window.speechSynthesis.speaking;
    },

    /**
     * Check if text-to-speech is currently paused
     * @returns {boolean} true if paused, false otherwise
     */
    ttsIsPaused: function() {
        console.debug("ttsIsPaused: Checking if speech synthesis is paused %s",window.speechSynthesis.paused ? "true" : "false");
        return window.speechSynthesis.paused;
    }
});
