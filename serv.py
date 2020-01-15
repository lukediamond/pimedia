import os
import cgi
import urllib
import socket
import struct

# decorator to handle socket connections
def message(func):
    def wrapper(*args):
        # initialize return value to none
        ret = None
        try:
            # attempt to connect socket
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0)
            sock.connect(("0.0.0.0", 44))
            # call the decorator's wrapped function, pass regular args + socket
            ret = func(sock, *args)
            # close connection
            sock.close()
        except:
            #handle connection error gracefully
            print("Failed to connect")
        return ret
    return wrapper

# decorated function to send song play request (message type 0)
@message
def play_song(sock, songname):
    sock.send(struct.pack("B", 0) + songname.encode("utf-8"))

# decorated function to send pause request (message type 1)
@message
def pause(sock):
    sock.send(struct.pack("B", 1))

# decorated function to send song resume request (message type 2)
@message
def resume(sock):
    sock.send(struct.pack("B", 2))

# decorated function to send seek request (message type 3)
@message
def seek(sock, timept):
    # send type and time point in fractional seconds (float dword)
    sock.send(struct.pack("<Bf", 3, timept))

# decorated function to query elapsed time (message type 4)
@message
def get_elapsed(sock):
    # send query
    sock.send(struct.pack("B", 4))
    # receive elapsed and return
    resp = sock.recv(4)
    if len(resp) < 4: return 0.0
    return struct.unpack("<f", resp)[0]

# decorated function to query song duration (message type 5)
@message
def get_duration(sock):
    # send query
    sock.send(struct.pack("B", 5))
    # receive duration and return
    resp = sock.recv(4)
    if len(resp) < 4: return 0.0
    return struct.unpack("<f", resp)[0]

# helper function to convert file extension to corresponding MIME type
def mime(path):
    if path.endswith(".js"): return "text/javascript"
    if path.endswith(".html"): return "text/html"
    if path.endswith(".svg"): return "image/svg+xml"
    return ""

# helper function to construct JSON list of available song paths
def get_listing():
    return "[" + ",".join("\"" + x + "\"" for x in os.listdir("converted")) + "]"


# UWSGI program entry point
# env            - environment variables (CGI)
# start_response - function to send HTTP response header 
def application(env, start_response):
    # fetch request method and URI from environment
    uri = env.get("REQUEST_URI", "")
    method = env.get("REQUEST_METHOD", "")
    # default URI to index.html
    if uri == "/": uri = "/index.html"

    # handle POST request
    if method == "POST":
        # fetch length of payload for relavent request types
        contentlen = int("0" + env.get("CONTENT_LENGTH", ""))
        # handle song list request
        if uri == "/getlist":
            start_response("200 OK", [("Content-Type", "text/json")])
            return [get_listing().encode("utf-8")]
        # handle song play request
        if uri == "/playsong":
            songpath = env["wsgi.input"].read(contentlen)
            songlen = play_song(songpath)
            start_response("200 OK", [])
            return [str(songlen).encode("utf-8")]
        # handle elapsed query
        if uri == "/getelapsed":
            start_response("200 OK", [("Content-Type", "text/plain")])
            return [str(get_elapsed()).encode("utf-8")]
        # handle duration query
        if uri == "/getduration":
            start_response("200 OK", [("Content-Type", "text/plain")])
            return [str(get_duration()).encode("utf-8")]
        # handle seek request
        if uri == "/seek":
            timept = float("0" + env["wsgi.input"].read(contentlen))
            seek(timept)
            start_response("200 OK", [])
            return [b""]
        # handle resume request
        if uri == "/resume":
            resume()
            start_response("200 OK", [])
            return [b""]
        # handle pause request
        if uri == "/pause":
            pause()
            start_response("200 OK", [])
            return [b""]
        # handle file upload
        if uri == "/upload":
            # create FieldStorage to facilitate file uploading
            form = cgi.FieldStorage(fp=env["wsgi.input"], environ=env)
            # fetch name/data and compute appropriate pathnames
            fname = form["file"].filename
            fdata = form["file"].value
            tmpname = "tmp/" + fname
            cvtname = "converted/" + "".join(fname.split(".")[:-1]) + ".raw"
            # write data to temporary media file
            with open(tmpname, "wb") as f:
                f.write(bytes(fdata))
            # run ffmpeg conversion from native format to 44.1khz 16-bit signed little-endian PCM
            os.system("ffmpeg -i \"" + tmpname + "\" -f s16le -acodec pcm_s16le -ar 44100 -ac 1 \"" + cvtname + "\"")
            #remove temporary file
            os.remove(tmpname)
            
            # prevent form reload, redirect to main page immediately
            start_response("301 Moved Permanently", [("Location", "/")])
            return [b""]

    # handle GET request
    if method == "GET":
        try:
            # read URI
            with open(uri[1:], "rb") as f:
                data = f.read()
            # send requested data with determined MIME type
            start_response("200 OK", [("Content-Type", mime(uri))]) 
            return data.encode("utf-8")
        except:
            # handle GET error gracefully
            start_response("404 Not Found", [("Content-Type", "text/plain")])
            return []
    # respond with empty message by default
    start_response("200 OK", [("Content-Type", "text/plain")])
    return []
