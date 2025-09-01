/*global Module*/
Module["arguments"] = [];


function httpShowProgressBar(filename) {
	// Reset and initialize the progress bar
	window.progressBarStartTime = Date.now();

	// Reset progress bar state
	document.getElementById("download-modal-progress-fill").style.transition = "none";
	document.getElementById("download-modal-progress-fill").style.width = "0%";

	// Set the filename in the modal title
	document.getElementById("download-modal-title").firstElementChild.innerHTML = filename;

	// Show the modal
	document.getElementById("download-modal").style.display = "block";

	// Enable smooth transition after a short delay
	setTimeout(() => {
		document.getElementById("download-modal-progress-fill").style.transition = "width 0.5s ease";
	}, 10);
};

function httpUpdateProgressBar(currentBytes, totalBytes) {
	if (window.progressBarStartTime === undefined) {
		window.progressBarStartTime = Date.now();
	}
	const units = ['bytes', 'KB', 'MB', 'GB'];
	function formatBytes(x) {
		let l = 0, n = parseInt(x, 10) || 0;
		while(n >= 1000 && ++l){
			n = n/1000;
		}
		return(n.toFixed(n < 10 && l > 0 ? 1 : 0) + ' ' + units[l]);
	};
	const progressPercent = (currentBytes / totalBytes) * 100 + "%";
	document.getElementById("download-modal-progress-fill").style.width = progressPercent;
	const progressText = "Downloaded " + formatBytes(currentBytes) + " / " + formatBytes(totalBytes);
	document.getElementById("download-modal-progress-text").innerHTML = progressText;

	// Calculate download speed
	const elapsedSeconds = (Date.now() - window.progressBarStartTime) / 1000;
	if (elapsedSeconds > 0) {
		const speedText = "Download speed: " + formatBytes( currentBytes / elapsedSeconds) + "/s";
		document.getElementById("download-modal-speed-text").innerHTML = speedText;
	}
};

function httpHideProgressBar() {
	// Hide the modal
	document.getElementById("download-modal").style.display = "none";

	// Reset progress bar for future use
	document.getElementById("download-modal-progress-fill").style.transition = "none";
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
	if (typeof input == "string" && (input.endsWith("scummvm.wasm"))) {
		filename = input.split("/").pop();
		return originalFetch(input, init).then((response) => {
			httpShowProgressBar("Loading ScummVM... ");
			var contentLength = response.headers.get("Content-Length");
			var totalBytes = parseInt(contentLength, 10);
			var loadedBytes = 0;

			var res = new Response( new ReadableStream( {
						async start(controller) {
							var reader = response.body.getReader();
							for (;;) {
								var { done, value } = await reader.read();
								if (done) {
									break;
								}
								controller.enqueue(value);
								loadedBytes += value.byteLength;
								httpUpdateProgressBar(loadedBytes, totalBytes);
							}
							httpHideProgressBar();
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
