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
 * This file implements Drag & Drop Filesystem C API functions in JavaScript for ScummVM's Emscripten port.
 * See: https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html#implement-a-c-api-in-javascript
 */

mergeInto(LibraryManager.library, {

    /**
     * Add drag and drop event listeners to the canvas
     * Also removes the SDL3 default drag&drop listeners to avoid conflicts
     * @returns {undefined} None
     */
    DragDropFilesystemNode_addDragEventListeners__deps: ['$ccall'],
    DragDropFilesystemNode_addDragEventListeners: function () {
        const target = Module.canvas;

        if (!target) {
            console.warn('Unable to locate drag event target');
            return;
        }
        if (!globalThis['dragdropentries']) {
            globalThis['dragdropentries'] = new Map();
            globalThis['dragdropentries'].set("/", new Set());
        }

        // Remove SDL3 default drag&drop dragEventListeners to avoid conflicts
        target.removeEventListener('dragover', Module.SDL3.eventHandlerDropDragover);
        target.removeEventListener('dragleave', Module.SDL3.eventHandlerDropDragend);
        target.removeEventListener('dragend', Module.SDL3.eventHandlerDropDragend);
        target.removeEventListener('drop', Module.SDL3.eventHandlerDropDrop);

        // UI Helpers
        const highlightTarget = () => {
            if (target && target.style) {
                target.style.opacity = 0.5;
                target.style.border = '3px dashed white';
            }
        };

        const resetTarget = () => {
            if (target && target.style) {
                target.style.opacity = 1;
                target.style.border = 'none';
            }
        };

        // Drag & Drop Event Listeners
        const dragEventListeners = {
            async drop(event) {
                event.preventDefault();
                resetTarget();
                async function scanFiles(item, path) {
                    path += item.name;
                    if (item.isDirectory) {
                        let directoryReader = item.createReader();
                        await directoryReader.readEntries(async (entries) => {
                            for (const entry of entries) {
                                await scanFiles(entry, path + "/");
                            }
                        });
                    }
                    globalThis['dragdropentries'].set(path, item)
                }
                let rootFolder = globalThis['dragdropentries'].get("/");
                let items = event.dataTransfer.items;
                for (const item of items) {
                    const entry = item.webkitGetAsEntry();
                    if (entry) {
                        await scanFiles(entry, "/");
                        rootFolder.add(entry);
                        ccall('DragDropFilesystemNode_pushDropFileEvent', null, ['string'], [entry.name]);
                    }
                }
                globalThis['dragdropentries'].set("/", rootFolder);
            },
            dragend() {
                resetTarget();
            },
            dragleave(event) {
                event.preventDefault();
                resetTarget();
            },
            dragover(event) {
                event.preventDefault();
                highlightTarget();
            }
        };

        // Register Listeners
        for (const [eventName, listener] of Object.entries(dragEventListeners)) {
            target.removeEventListener(eventName, listener); // make sure we arent' adding duplicates
            target.addEventListener(eventName, listener);
        }

        globalThis['dragEventListeners'] = dragEventListeners;
    },

    /**
     * Remove drag and drop event listeners from the canvas
     * @returns {undefined} None
     */
    DragDropFilesystemNode_removeDragEventListeners: function () {
        const target = Module.canvas;
        if (!target) {
            return;
        }
        // reset UI in case this is executed during a drag operation
        if (target.style) {
            target.style.opacity = 1;
            target.style.border = 'none';
        }
        if (globalThis['dragEventListeners']) {
            for (const [eventName, listener] of Object.entries(globalThis['dragEventListeners'])) {
                target.removeEventListener(eventName, listener);
            }
            delete globalThis['dragEventListeners'];
        }
    },

    /**
     * Check if a file or directory exists in the drag&drop filesystem
     * @param {number} pathPtr - pointer to path string in WASM memory
     * @returns {number} 1 if exists, 0 if not
     */
    DragDropFilesystemNode_exists: function (pathPtr) {
        const path = UTF8ToString(pathPtr);
        return globalThis['dragdropentries'].has(path) ? 1 : 0;
    },

    /**
     * Check if a path represents a directory in the drag&drop filesystem
     * @param {number} pathPtr - pointer to path string in WASM memory
     * @returns {number} 1 if directory, 0 if not
     */
    DragDropFilesystemNode_isDirectory: function (pathPtr) {
        const path = UTF8ToString(pathPtr);
        const entry = globalThis['dragdropentries'].get(path);
        return (entry && entry.isDirectory) ? 1 : 0;
    },

    /**
     * Get the size of a file in the drag&drop filesystem
     * @param {number} pathPtr - pointer to path string in WASM memory
     * @returns {number} file size in bytes, 0 for directories or non-existent files
     */
    DragDropFilesystemNode_size__async: true,
    DragDropFilesystemNode_size: (pathPtr) => Asyncify.handleSleep(async (wakeUp) => {
        const path = UTF8ToString(pathPtr);

        const entry = globalThis['dragdropentries'].get(path);
        if (!entry || !entry.isFile) {
            console.error('Path is not a file in drag&drop filesystem', path);
            wakeUp(-1);
        }

        entry.file(async (file) => {
            wakeUp(file.size);
        }, (error) => {
            console.error('Error getting file size from drag&drop filesystem', path, error);
            wakeUp(-1);
        });
    }),
    DragDropFilesystemNode_getChildren__async: true,
    DragDropFilesystemNode_getChildren__deps: ['malloc', '$setValue', '$lengthBytesUTF8', '$stringToUTF8Array'],
    DragDropFilesystemNode_getChildren: (pathPtr) => Asyncify.handleSleep(async (wakeUp) => {
        const path = UTF8ToString(pathPtr);
        let folderEntry = globalThis['dragdropentries'].get(path);
        let children = []; // dummy data

        if (path == "/") {
            for (const child of folderEntry.keys()) {

                console.log(child);
                children.push(child.name);
            }
        } else if (folderEntry && folderEntry.isDirectory) {
            let directoryReader = folderEntry.createReader();
            let readEntries = (directoryReader) => {
                return new Promise((resolve, reject) => {
                    directoryReader.readEntries(resolve, reject)

                });
            }
            let entries = []
            do {
                entries = await readEntries(directoryReader);
                entries.forEach((entry) => {
                    children.push(entry.name);
                });
            } while (entries.length > 0);
        }
        children.push(""); // we need this to find the end of the array on the native side.

        // convert the strings to C strings
        var c_strings = children.map((s) => {
            var size = lengthBytesUTF8(s) + 1;
            var ret = _malloc(size);
            stringToUTF8Array(s, HEAP8, ret, size);
            return ret;
        });

        var ret_arr = _malloc(c_strings.length * 4); // 4-bytes per pointer
        c_strings.forEach((ptr, i) => { setValue(ret_arr + i * 4, ptr, "i32"); }); // populate return array
        wakeUp(ret_arr);

    }),


    /**
     * Read data from a file in the drag&drop filesystem
     * @param {number} pathPtr - pointer to file path string in WASM memory
     * @param {number} offset - byte offset to start reading from
     * @param {number} size - number of bytes to read
     * @param {number} bufferPtr - pointer to buffer in WASM memory to write data
     * @returns {number} actual number of bytes read
     */
    DragDropFilesystemNode_readFile__async: true,
    DragDropFilesystemNode_readFile: (pathPtr, offset, size, bufferPtr) => Asyncify.handleSleep(async (wakeUp) => {
        const path = UTF8ToString(pathPtr);
        const entry = globalThis['dragdropentries'].get(path);

        if (!entry || !entry.isFile) {
            wakeUp(0);
        }
        entry.file(async (file) => {
            const blob = file.slice(offset, offset + size);
            const arrbuffer = await blob.arrayBuffer();
            const arr = new Uint8Array(arrbuffer);
            if (arr.length > 0) {
                HEAPU8.set(arr, bufferPtr);
            }
            wakeUp(arr.length);
        }, (error) => {
            console.error('Error reading file from drag&drop filesystem', path, error);
            wakeUp(0);
        });
    })
});
