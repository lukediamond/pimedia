// standard library includes
#include <stdint.h>
#include <assert.h>
#include <stdio.h>

// misc/file includes
#include <unistd.h>
#include <fcntl.h>
// network includes
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
// multithreading include
#include <pthread.h>

// third-party includes
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <wiringPi.h>

const int SAMPLE_RATE = 44100;
const int CHUNK_SIZE = SAMPLE_RATE * 2;


/*
 * PLAY THREAD
 */

bool playthread_active = false;
pthread_t playthread;

struct PlayThreadArgs {
	int fd;
	Mix_Chunk chunk;
	int16_t* backbuf;
};

void* playthread_func(void* pargs) {
	PlayThreadArgs& args = *(PlayThreadArgs*) pargs;

	while (playthread_active) {
		int numread = read(args.fd, args.backbuf, CHUNK_SIZE * sizeof(int16_t));
		if (numread <= 0) break;
		args.chunk.alen = numread;
		while (playthread_active && (Mix_Paused(0) || Mix_Playing(0))) usleep(10000);
		if (!playthread_active) break;
		int16_t* oldbuf = (int16_t*) args.chunk.abuf;
		args.chunk.abuf = (Uint8*) args.backbuf;
		args.backbuf = oldbuf;
		Mix_PlayChannel(0, &args.chunk, 0);
	} 
	playthread_active = false;

	return NULL;
}

void playthread_stop() {
	if (playthread_active) {
		playthread_active = false;
		pthread_join(playthread, NULL);
	}
}

void playthread_wait() {
	pthread_join(playthread, NULL);
}

void playthread_start(PlayThreadArgs& args) {
	if (playthread_active) playthread_stop(); 
	playthread_active = true;
	pthread_create(&playthread, NULL, playthread_func, &args);
}

/*
 * MESSAGE FORMAT
 */

// message types
const uint8_t MT_PLAY = 0;
const uint8_t MT_PAUSE = 1;
const uint8_t MT_RESUME = 2;

struct Message_Play {
	char filename[128];
};

int main() {
	PlayThreadArgs ptargs;	

	printf("initializing audio subsystem...");
	{
		// initialize SDL and SDL_mixer
		SDL_Init(SDL_INIT_AUDIO);
		Mix_Init(0);

		// Open 44.1khz, single-channel audio stream
		if (Mix_OpenAudio(SAMPLE_RATE, AUDIO_S16SYS, 1, 1024) != 0) {
			printf("\nfailed to initialize audio\n");	
			return 1;
		}

		// allocate one audio channel
		Mix_AllocateChannels(1);

		ptargs.fd = -1;
		ptargs.chunk.allocated = 0;
		ptargs.chunk.abuf = (Uint8*) new int16_t[CHUNK_SIZE];
		ptargs.chunk.volume = 128;
		ptargs.backbuf = new int16_t[CHUNK_SIZE];
	}
	printf(" done\n");


	printf("initializing server socket...");
	int sock;
	{
		struct sockaddr_in addr;
		int opt = 1;

		// create socket and check for valid descriptor
		sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock < 0) {
			perror("\nfailed to create socket");
			return 1;
		}
		// allow subsequent launches to use the same address
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
		// set up IPv4 address
		addr.sin_family = AF_INET;
		addr.sin_port = htons(44);
		memset(&addr.sin_addr, 0, 4);
		// bind to address and check for errors
		if (bind(sock, (sockaddr*) &addr, sizeof(addr)) != 0) {
			perror("\nfailed to bind socket");
			return 1;
		}
		// listen for connections and check for errors
		if (listen(sock, 1) != 0) {
			perror("\nsocket failed to listen");
			return 1;
		}
	}
	printf(" done\n");

	// listen for messages
	for (;;) {
		// accept incoming connection
		int remote = accept(sock, NULL, NULL);
		uint8_t mtype;

		// determine type of message (first byte)
		read(remote, &mtype, 1);
		// handle message type, reading additional data
		if (mtype == MT_PLAY) {
			Message_Play mesg = {0};


			read(remote, &mesg, sizeof(mesg));
			printf("received play request: %s\n", mesg.filename);
			ptargs.fd = open(mesg.filename, O_RDONLY);	
			if (ptargs.fd > 0) 
				playthread_start(ptargs);
			else
				printf("invalid file name: %s\n", mesg.filename);
		}
		if (mtype == MT_PAUSE) {
			printf("received pause request\n");
			// pause audio if audio is playing
			if (playthread_active)
				Mix_Pause(0);
		}
		if (mtype == MT_RESUME) {
			printf("received resume request\n");
			// resume audio if audio was playing
			if (playthread_active)
				Mix_Resume(0);
		}
		// close remote connection
		close(remote);
	}

	// close audio file
	if (ptargs.fd > 0) close(ptargs.fd);
	// free memory
	delete[] (int16_t*) ptargs.chunk.abuf;
	delete[] ptargs.backbuf;
	// close networking
	close(sock);
	// close audio subsystem and SDL
	Mix_CloseAudio();
	Mix_Quit();
	SDL_Quit();

}
