# Raspberry Pi Audio Server

### Dependencies:
* clang
* python 3
* SDL2_mixer
* SDL2
* nginx
* UWSGI
* ffmpeg

To install, run:
```bash
sudo apt install clang python3 libsdl2-dev libsdl2-mixer-dev uwsgi uwsgi-plugin-python nginx ffmpeg
```

### Building:

The command to build the audio server is:
```bash
# in project directory
./build.sh
```
### Setup:

Once the nginx server is set up, you have to configure it to forward to the UWSGI endpoint.
A default raspbian install of nginx will have its default config at `/etc/nginx/sites-enabled/default`.
This file should contain:

```
server {
	listen 80 default_server;
	listen [::]:80 default_server;

	location / {
		uwsgi_pass 127.0.0.1:8080;
		include uwsgi_params;
	}
}
```

Run `sudo systemctl restart nginx.service` once the config is edited.

### Starting Server:

There are two components to the server that must be run simultaneously:
* the audio server
* the UWSGI server

In the project directory, run:
```bash
sudo ./main
```
to start the audio server, and
```bash
./start_server.sh
```
to start the UWSGI server.
