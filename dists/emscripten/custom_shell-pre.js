/*global Module*/
Module["arguments"] = [];

// Shared utility function for formatting bytes - used by both pre-js and library code
const units = ['bytes', 'KB', 'MB', 'GB'];
globalThis.formatBytes = function(x) {
	let l = 0, n = parseInt(x, 10) || 0;
	while(n >= 1000 && ++l){
		n = n/1000;
	}
	return(n.toFixed(n < 10 && l > 0 ? 1 : 0) + ' ' + units[l]);
};

globalThis.httpShowProgressBar = function (filename) {
	// Reset and initialize the progress bar
	window.progressBarStartTime = Date.now();

	// Reset progress bar state
	document.getElementById("download-modal-progress-fill").style.transition =
		"none";
	document.getElementById("download-modal-progress-fill").style.width = "0%";

	// Set the filename in the modal title
	document.getElementById("download-modal-title").firstChild.innerHTML =
		filename;

	// Show the modal
	document.getElementById("download-modal").style.display = "block";

	// Enable smooth transition after a short delay
	setTimeout(() => {
		document.getElementById(
			"download-modal-progress-fill"
		).style.transition = "width 0.5s ease";
	}, 10);
};

globalThis.httpUpdateProgressBar = function (current, total) {
	if (window.progressBarStartTime === undefined) {
		window.progressBarStartTime = Date.now();
	}

	const progressPercent = (current / total) * 100 + "%";
	document.getElementById("download-modal-progress-fill").style.width =
		progressPercent;
	document.getElementById("download-modal-progress-text").innerHTML =
		"Downloaded " +
		globalThis.formatBytes(current) +
		" / " +
		globalThis.formatBytes(total);

	// Calculate download speed
	const elapsedSeconds = (Date.now() - window.progressBarStartTime) / 1000;
	if (elapsedSeconds > 0) {
		const speed = current / elapsedSeconds;
		document.getElementById("download-modal-speed-text").innerHTML =
			"Download speed: " + globalThis.formatBytes(speed) + "/s";
	}
};

globalThis.httpHideProgressBar = function () {
	// Hide the modal
	document.getElementById("download-modal").style.display = "none";

	// Reset progress bar for future use
	document.getElementById("download-modal-progress-fill").style.transition =
		"none";
	document.getElementById("download-modal-progress-fill").style.width = "0%";

	// Reset the start time
	window.progressBarStartTime = undefined;
};

// Add all parameters passed via the fragment identifier
if (window.location.hash.length > 0) {
    params = decodeURI(window.location.hash.substring(1)).split(" ")
    params.forEach((param) => {
        Module["arguments"].push(param);
    })
}

// Reload page when fragment identifier (and launch parameters) changes
window.addEventListener("hashchange", function () {
	location.reload(); 
});

// Loading Indicator for WASM Initialization
// To get the progress of WebAssembly.instantiateStreaming / WebAssembly.compileStreaming,
// we hook the browser fetch API and create a new Fetch Response with a custom ReadableStream which implements it's own controller.
// This is only used for .wasm (the main wasm file and .so files - plugins)

var originalFetch = fetch;
fetch = (input, init) => {
	if (typeof input == "string" && (input.endsWith(".wasm") || input.endsWith(".so"))) {
		startTime = Date.now();
		filename = input.split("/").pop();
		return originalFetch(input, init).then((response) => {
			document.getElementById("download-modal-title").firstChild.innerHTML = filename;
			document.getElementById('download-modal').style.display = 'block';
			var contentLength = response.headers.get("Content-Length");
			var total = parseInt(contentLength, 10);
			var loaded = 0;

			function progressHandler(bytesLoaded, totalBytes) {
				document.getElementById("download-modal-progress-fill").style.width = (bytesLoaded / totalBytes) * 100 + "%";
				document.getElementById("download-modal-progress-text").innerHTML = "Downloaded " + globalThis.formatBytes(bytesLoaded) + " / " + globalThis.formatBytes(totalBytes);
				document.getElementById("download-modal-speed-text").innerHTML = "Download speed: " + globalThis.formatBytes(bytesLoaded / ((Date.now() - startTime) / 1000)) + "/s";
			}

			var res = new Response( new ReadableStream( {
						async start(controller) {
							var reader = response.body.getReader();
							for (;;) {
								var { done, value } = await reader.read();

								if (done) {
									progressHandler(total, total);
									break;
								}

								controller.enqueue(value);
								loaded += value.byteLength;
								progressHandler(loaded, total);
							}
							document.getElementById('download-modal').style.display = 'none';
							controller.close();
						},
					},
					{
						status: response.status,
						statusText: response.statusText,
					}
				)
			);
			// Wasm is very picky with it's headers and it will fail to compile if they are not
			// specified correctly.
			for (var pair of response.headers.entries()) {
				res.headers.set(pair[0], pair[1]);
			}
			return res;
		});
	} else {
		return originalFetch(input, init);
	}
};
