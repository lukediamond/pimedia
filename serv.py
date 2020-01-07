import sys
sys.path.append("/usr/local/lib/python3.7/dist-packages")
sys.path.append("/usr/lib/python3/dist-packages")

import socket
import struct
import os
import cgi

# define headers for various message types
HEADER_PLAY = struct.pack("B", 0)
HEADER_PAUSE = struct.pack("B", 1)
HEADER_RESUME = struct.pack("B", 2)

# open socket and send message 
def send_message(mesg):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0)
    sock.connect(("0.0.0.0", 44))
    sock.send(mesg)

source = \
"""
<html>
    <form action="/" method="POST" enctype="multipart/form-data">
        <input name="file" accept=".mp3,.ogg,.flac,.wav" type="file"></input>
        <input type="submit"></input>
    </form>
</html>
"""


def application(env, start_response):
    start_response("200 OK", [("Content-Type", "text/html")])
    method = env.get("REQUEST_METHOD", "")
    uri = env.get("REQUEST_URI", "")
    contentlen = int("0" + env.get("CONTENT_LENGTH", ""))
    if method == "POST":
        fs = cgi.FieldStorage(fp=env["wsgi.input"], environ=env)
        name = fs["file"].filename
        data = fs["file"].value
        tmpname = "tmp/" + name
        outname = "converted/" + ".".join(name.split(".")[:-1]) + ".raw"
        with open(tmpname, "wb") as f:
            f.write(data)
        os.system("ffmpeg -i " + tmpname + " -y -f s16le -acodec pcm_s16le -ac 1 -ar 44100 " + outname)
        os.remove(tmpname)
        send_message(HEADER_PLAY + outname.encode("utf-8"))
        
    return [source.encode("utf-8")]


