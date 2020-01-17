// global variable holding duration of current clip in seconds
var duration = 0;

// send request to play song
function playSong(e) {
    duration = 0;
    // update "now playing" text
    document.getElementById("nowplaying").innerHTML = 
        "Now Playing: " + e.target.innerHTML;
    let songpath = `converted/${e.target.getAttribute("data-songname")}`;
    // create and send AJAX POST request to /playsong
    let xhr = new XMLHttpRequest();
    xhr.overrideMimeType("text/plain");
    xhr.open("POST", "/playsong");
    xhr.send(songpath);
    // update duration variable
    getDuration((dur) => { duration = dur; });
}

// programmatically send file upload form
function upload() {
    document.getElementById("fileform").submit();
}

// global variable to disable updating seek bar when under user control 
let seeking = false;

// send seek request
function seek(e) {
    seeking = true;
    // do nothing if current song duration is invalid
    if (duration <= 0) { 
        seeking = false;
        return;
    }
    // calculate time point from slider value
    let point = (document.getElementById("progress").value * duration) / 100;
    
    // create and send AJAX post request to /seek
    let xhr = new XMLHttpRequest();
    xhr.overrideMimeType("text/plain");
    xhr.open("POST", "/seek");
    xhr.onload = () => {
        // disable seeking when request returns
        seeking = false;
    };
    xhr.send(point.toString());
}

// get song duration and return via callback
function getDuration(cb) {
    // create and send AJAX POST request to /getduration
    let xhr = new XMLHttpRequest();
    xhr.overrideMimeType("text/plain");
    xhr.open("POST", "/getduration");
    xhr.onload = () => {
        // return fetched response
        cb(parseFloat(xhr.response));
    };
    xhr.send();
}

// get elapsed song time and return via callback
function getElapsed(cb) {
    // create and send AJAX POST request to /getelapsed
    let xhr = new XMLHttpRequest();
    xhr.overrideMimeType("text/plain");
    xhr.open("POST", "/getelapsed");
    xhr.onload = () => {
        // return fetched response
        cb(parseFloat(xhr.response));
    };
    xhr.send();
}

// pause currently playing song
function pause() {
    // create and send AJAX POST request to /pause
    let xhr = new XMLHttpRequest();
    xhr.open("POST", "/pause");
    xhr.overrideMimeType("text/json");
    xhr.send();
}

// resume playing song
function resume() {
    // create and send AJAX POST request to /resume
    let xhr = new XMLHttpRequest();
    xhr.open("POST", "/resume");
    xhr.overrideMimeType("text/json");
    xhr.send();
}

// update list of songs
function updateSongList() {
    // create and send AJAX POST request to /getlist
    let xhr = new XMLHttpRequest();
    xhr.open("POST", "/getlist");
    // send data as JSON rather than XML
    xhr.overrideMimeType("text/json");
    xhr.onload = () => {
        // parse returned JSON list
        let songs = JSON.parse(xhr.response);
        // get song list parent element, and clear children
        let songList = document.getElementById("songlist");
        while (songList.hasChildNodes())
            songList.removeChild(songList.lastChild);
        // create and add song elements
        for (let song of songs) {
            let entry = document.createElement("LI");
            entry.classList.add("noselect");
            // set custom attribute for song filepath
            entry.setAttribute("data-songname", song);
            let dotsplit = song.split(".");
            dotsplit.pop();
            entry.innerHTML = dotsplit.join(".");
            entry.onclick = playSong;
            // add element to parent
            songlist.appendChild(entry);
        }
    }
    xhr.send();
}

// helper function: converts seconds to mm:ss time point string
function getTimeString(secs) {
    let min = Math.floor(secs / 60);
    let sec = Math.floor(secs % 60);
    return min + ":" + ("00" + sec).substr(-2);
}

// initialize song list and duration 
updateSongList();
getDuration((dur) => { duration = dur; });

// active loop to update live content
setInterval(() => {
    // get elapsed time
    getElapsed((timePoint) => {
        // update song elapsed time display
        document.getElementById("elapsed").innerHTML = getTimeString(timePoint);
        // update song duration display
        document.getElementById("duration").innerHTML = getTimeString(duration);
        // cancel further updates if user is seeking
        if (seeking) return;
        // update progress slider/seek bar position
        document.getElementById("progress").value = 100 * (timePoint / duration) || 0;
    });
}, 32);
