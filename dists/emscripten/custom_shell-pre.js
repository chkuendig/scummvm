// Handle launch parameters passed via the fragment identifier (when not in the worker context)
if (!ENVIRONMENT_IS_WORKER) {

    /*global Module*/
    Module["arguments"] = []; // default arguments

    // Add all parameters passed via the fragment identifier
    if (window.location.hash.length > 0) {
        params = decodeURI(window.location.hash.substring(1)).split(" ")
        params.forEach((param) => {
            Module["arguments"].push(param);
        })
    }

    window.addEventListener("hashchange", function () {
        location.reload(); // presumably the launch parameters changed
    });
}