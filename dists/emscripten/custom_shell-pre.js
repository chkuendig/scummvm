/*global Module*/
Module["arguments"] = [];

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
