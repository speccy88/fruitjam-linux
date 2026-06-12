"use strict";

/*
 * The Fruit Jam dashboard is self-contained in index.html.  This file remains
 * only so older cached pages that still request /app.js do not run stale code.
 */
window.FruitJam = window.FruitJam || {
	bind: function () {
		if (!window.location)
			return;
		if (window.location.search.indexOf("ui=inline") >= 0)
			return;
		var host = window.location.hostname || "127.0.0.1";
		var origin = window.location.protocol === "file:" ? "http://" + host : "";
		window.location.replace(origin + "/?ui=inline-" + Date.now());
	}
};
