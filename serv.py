import os
import cgi
import urllib
import socket
import struct

def play_song(songname):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0)
    sock.connect(("0.0.0.0", 44))
    sock.send(struct.pack("B", 0) + songname.encode("utf-8"))
    size = struct.unpack("L", sock.recv(4))
    print(size)
    sock.close()
    return size

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

    print(get_listing())
    if method == "POST":
        if uri == "/getlist":
            start_response("200 OK", [("Content-Type", "text/json")])
            return [get_listing().encode("utf-8")]
        if uri == "/playsong":
            contentlen = int("0" + env.get("CONTENT_LENGTH", ""))
            songpath = env["wsgi.input"].read(contentlen)
            play_song(songpath)
            start_response("200 OK", [])
            return []

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
