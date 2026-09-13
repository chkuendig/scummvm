#!/bin/bash
# Focused regression test for the post-link Asyncify re-entrancy guards that
# build.sh patches into the generated scummvm.js (see the "make"/"build" task,
# search for "Asyncify.State.Normal"). Runs against a fixture containing the
# exact SDL3 6.0.2 emscripten audio backend snippets (silence_callback plus
# the two real onaudioprocess handlers) instead of a full Emscripten build,
# so it can run without the toolchain and still catch anchor drift if the
# emsdk-generated JS is ever reformatted.
#
# Usage: dists/emscripten/test-asyncify-audio-guard.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TMP_JS="$(mktemp /tmp/asyncify-guard-fixture.XXXXXX.js)"
trap 'rm -f "${TMP_JS}"' EXIT

# Minimal fixture reproducing the three raw dynCall('ip', ...) sites emitted
# by SDL_emscriptenaudio.c (recording onaudioprocess, playback onaudioprocess,
# and one of the two silence_callback fallback timers).
cat > "${TMP_JS}" <<'EOF'
var recordingSetup = function() { SDL3.audio_recording.scriptProcessorNode.onaudioprocess = function(audioProcessingEvent) { if ((SDL3 === undefined) || (SDL3.audio_recording === undefined)) { return; } audioProcessingEvent.outputBuffer.getChannelData(0).fill(0.0); SDL3.audio_recording.currentRecordingBuffer = audioProcessingEvent.inputBuffer; dynCall('ip', $2, [$3]); }; };
var playbackSetup = function() { SDL3.audio_playback.scriptProcessorNode['onaudioprocess'] = function (e) { if ((SDL3 === undefined) || (SDL3.audio_playback === undefined)) { return; } SDL3.audio_playback.currentPlaybackBuffer = e['outputBuffer']; dynCall('ip', $2, [$3]); }; };
var silence_callback = function() { SDL3.audio_recording.currentRecordingBuffer = SDL3.audio_recording.silenceBuffer; dynCall('ip', $2, [$3]); };
EOF

apply_patches() {
  local js="$1"
  if ! grep -q 'silence_callback = function() { if (typeof Asyncify' "${js}"; then
    sed -i 's/var silence_callback = function() {/var silence_callback = function() { if (typeof Asyncify !== "undefined" \&\& Asyncify.state !== Asyncify.State.Normal) return;/g' "${js}"
  fi
  if ! grep -q 'function(audioProcessingEvent) { if (typeof Asyncify' "${js}"; then
    sed -i 's/function(audioProcessingEvent) { if ((SDL3 === undefined) || (SDL3.audio_recording === undefined)) { return; }/function(audioProcessingEvent) { if (typeof Asyncify !== "undefined" \&\& Asyncify.state !== Asyncify.State.Normal) return; if ((SDL3 === undefined) || (SDL3.audio_recording === undefined)) { return; }/' "${js}"
  fi
  if ! grep -q 'function (e) { if (typeof Asyncify' "${js}"; then
    sed -i 's/function (e) { if ((SDL3 === undefined) || (SDL3.audio_playback === undefined)) { return; }/function (e) { if (typeof Asyncify !== "undefined" \&\& Asyncify.state !== Asyncify.State.Normal) return; if ((SDL3 === undefined) || (SDL3.audio_playback === undefined)) { return; }/' "${js}"
  fi
}

fail() { echo "FAIL: $1"; exit 1; }

apply_patches "${TMP_JS}"

grep -q 'function(audioProcessingEvent) { if (typeof Asyncify !== "undefined" && Asyncify.state !== Asyncify.State.Normal) return; if ((SDL3 === undefined)' "${TMP_JS}" \
  || fail "recording onaudioprocess was not guarded"
grep -q 'function (e) { if (typeof Asyncify !== "undefined" && Asyncify.state !== Asyncify.State.Normal) return; if ((SDL3 === undefined)' "${TMP_JS}" \
  || fail "playback onaudioprocess was not guarded"
grep -q 'silence_callback = function() { if (typeof Asyncify !== "undefined" && Asyncify.state !== Asyncify.State.Normal) return;' "${TMP_JS}" \
  || fail "silence_callback guard regressed"

BEFORE_SUM="$(md5sum "${TMP_JS}")"
apply_patches "${TMP_JS}"
AFTER_SUM="$(md5sum "${TMP_JS}")"
[[ "${BEFORE_SUM}" == "${AFTER_SUM}" ]] || fail "patches are not idempotent (re-running changed the file)"

if command -v node >/dev/null 2>&1; then
  node --check "${TMP_JS}" || fail "patched fixture is not valid JavaScript"
fi

echo "OK: asyncify audio guard patches apply correctly and idempotently"
