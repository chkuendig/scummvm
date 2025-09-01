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
 * JavaScript support functions for the Emscripten filesystem factory.
 */

mergeInto(LibraryManager.library, {
    /**
     * Initialize filesystem settings stored in IDBFS.
     * @param {number} pathPtr - pointer to the settings path string in WASM memory.
     */
    EmscriptenFilesystemFactory_initDefaultConfigFile__async: true,
    EmscriptenFilesystemFactory_initDefaultConfigFile: (pathPtr) => Asyncify.handleSleep(async (wakeUp) => {
        try {
            const settingsPath = UTF8ToString(pathPtr);
            const path = settingsPath.substring(0, settingsPath.lastIndexOf('/'));

            // Mount the filesystem.
            FS.mount(IDBFS, { autoPersist: true }, path);

            // Sync the filesystem.
            await new Promise((resolve, reject) => {
                FS.syncfs(true, (err) => err ? reject(err) : resolve());
            });

            // Check if the settings file exists and download a default copy if needed.
            if (!FS.analyzePath(settingsPath).exists) {
                console.debug('EmscriptenFilesystemFactory_initDefaultConfigFile');
                const response = await fetch('scummvm.ini');
                if (response.ok) {
                    const text = await response.text();
                    FS.writeFile(settingsPath, text);
                }
            }
            console.debug('Filesystem initialized at %s', path);
        } catch (err) {
            console.error('Error initializing files:', err);
            alert('Error initializing files: ' + err);
            throw err;
        }
        wakeUp();
    })
});
