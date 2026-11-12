/* ES5 syntax for the PS3 browser. This animation has no audio input. */
(function () {
    "use strict";
    var pictures = ["art/miku-city.jpg", "art/miku-cosmic.jpg", "art/miku-piano.jpg"];
    var selected = 0, moving = true, start = new Date().getTime();
    var canvas = document.getElementById("effects"), context = null;
    var controls = document.getElementById("controls"), art = document.getElementById("art");
    var stars = [], width = 1280, height = 720, i;
    try { context = canvas.getContext("2d"); } catch (ignore) {}
    for (i = 0; i < 48; i += 1) {
        stars.push({x: ((i * 97 + 13) % 997) / 997, y: ((i * 59 + 31) % 991) / 991,
                    speed: 3 + (i % 5), radius: 1 + (i % 3) * 0.4});
    }
    function resize() {
        width = Math.min(window.innerWidth || 1280, 1920);
        height = Math.min(window.innerHeight || 720, 1080);
        canvas.width = width; canvas.height = height;
    }
    window.choose = function (index) {
        selected = (index + pictures.length) % pictures.length;
        var image = new Image();
        image.onload = function () {
            art.style.backgroundImage = "url('" + pictures[selected] + "')";
            document.getElementById("status").innerHTML = "Artwork loaded. Start music from XMB if it is not already playing.";
        };
        image.onerror = function () {
            document.getElementById("status").innerHTML = "Artwork missing. Run tools/fetch_art.py before installing.";
        };
        image.src = pictures[selected];
    };
    window.hideControls = function () {
        controls.style.display = "none";
        document.getElementById("hint").style.display = "none";
    };
    window.toggleMotion = function () {
        moving = !moving;
        if (!moving && context) { context.clearRect(0, 0, width, height); }
    };
    document.onkeydown = function (event) {
        var key = (event || window.event).keyCode;
        if (key === 37) { window.choose(selected - 1); }
        if (key === 39) { window.choose(selected + 1); }
        if (key === 32) { window.toggleMotion(); }
        if (key === 13) {
            controls.style.display = controls.style.display === "none" ? "block" : "none";
        }
    };
    window.onresize = resize;
    resize(); window.choose(0);
    window.setInterval(function () {
        if (!moving || !context) { return; }
        var time = (new Date().getTime() - start) / 1000, s, x, y, alpha;
        context.clearRect(0, 0, width, height);
        for (i = 0; i < stars.length; i += 1) {
            s = stars[i]; x = s.x * width + Math.sin(time * 0.15 + i) * 12;
            y = ((s.y * height - time * s.speed) % height + height) % height;
            alpha = 0.14 + (Math.sin(time * 0.8 + i) + 1) * 0.16;
            context.fillStyle = "rgba(186,225,255," + alpha + ")";
            context.beginPath(); context.arc(x, y, s.radius, 0, Math.PI * 2, false); context.fill();
        }
    }, 80);
}());
