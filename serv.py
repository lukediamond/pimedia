import os
import cgi
import urllib
import socket
import struct

def message(func):
    def wrapper(*args):
        ret = None
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0)
            sock.connect(("0.0.0.0", 44))
            ret = func(sock, *args)
            sock.close()
        except: pass
        return ret
    return wrapper

@message
def play_song(sock, songname):
    sock.send(struct.pack("B", 0) + songname.encode("utf-8"))
    return struct.unpack("L", sock.recv(4))[0]
@message
def seek(sock, sample):
    sock.send(struct.pack("<BL", 3, sample))
@message
def get_elapsed(sock):
    sock.send(struct.pack("B", 4))
    resp = sock.recv(4)
    if len(resp) < 4: return 0
    return struct.unpack("L", resp)[0]

def mime(path):
    if path.endswith(".js"): return "text/javascript"
    if path.endswith(".html"): return "text/html"
    return ""

def get_listing():
    return "[" + ",".join("\"" + x + "\"" for x in os.listdir("converted")) + "]"


def application(env, start_response):
    uri = env.get("REQUEST_URI", "")
    method = env.get("REQUEST_METHOD", "")
    if uri == "/": uri = "/index.html"

    if method == "POST":
        contentlen = int("0" + env.get("CONTENT_LENGTH", ""))
        if uri == "/getlist":
            start_response("200 OK", [("Content-Type", "text/json")])
            return [get_listing().encode("utf-8")]
        if uri == "/playsong":
            songpath = env["wsgi.input"].read(contentlen)
            songlen = play_song(songpath)
            start_response("200 OK", [])
            return [str(songlen).encode("utf-8")]
        if uri == "/getelapsed":
            start_response("200 OK", [("Content-Type", "text/plain")])
            return [str(get_elapsed()).encode("utf-8")]
        if uri == "/seek":
            sample = int(round(float("0" + env["wsgi.input"].read(contentlen))))
            print(struct.pack("L", sample))
            seek(sample)
            start_response("200 OK", [])
            return [b""]


    if method == "GET":
        try:
            with open(uri[1:], "r") as f:
                data = f.read()
            start_response("200 OK", [("Content-Type", mime(uri))]) 
            return data.encode("utf-8")
        except:
            start_response("404 Not Found", [("Content-Type", "text/plain")])
            return []
    start_response("200 OK", [("Content-Type", "text/plain")])
    return []
