
/* standard library includes */
#include <stdint.h>
#include <assert.h>
#include <stdio.h>

/* misc/file includes */
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
/* network includes */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
/* multithreading include */
#include <pthread.h>

/* third-party includes */
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <wiringPi.h>

const int SAMPLE_RATE = 44100;
const int CHUNK_SIZE = SAMPLE_RATE * 2;

uint64_t nanos() {
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

/*
 * PLAY THREAD
 */

int playthread_active = 0;
pthread_t playthread;

typedef struct {
    int fd;
    Mix_Chunk chunk;
    int16_t* backbuf;
} PlayThreadArgs;

uint64_t played_elapsed = 0;
void* playthread_func(void* pargs) {
    PlayThreadArgs* args = (PlayThreadArgs*) pargs;

    while (playthread_active) {
        int16_t* oldbuf;
        int numread;

        numread = read(args->fd, args->backbuf, CHUNK_SIZE * sizeof(int16_t));
        if (numread <= 0) break;
        args->chunk.alen = numread;
        if (!playthread_active) break;
        oldbuf = (int16_t*) args->chunk.abuf;
        args->chunk.abuf = (Uint8*) args->backbuf;
        args->backbuf = oldbuf;
        Mix_PlayChannel(0, &args->chunk, 0);
        while (playthread_active && (Mix_Paused(0) || Mix_Playing(0))) {
            usleep(10000);
            if (!Mix_Paused(0) && Mix_Playing(0))
                played_elapsed += 10000;
        }
    } 
    playthread_active = 0;

    return NULL;
}

void playthread_stop() {
    if (playthread_active) {
        playthread_active = 0;
        pthread_join(playthread, NULL);
    }
}

void playthread_wait() {
    pthread_join(playthread, NULL);
}

void playthread_start(PlayThreadArgs* args) {
    if (playthread_active) playthread_stop(); 
    playthread_active = 1;
    pthread_create(&playthread, NULL, playthread_func, args);
}

/*
 * MESSAGE FORMAT
 */

/* message types */
const uint8_t MT_PLAY = 0;
const uint8_t MT_PAUSE = 1;
const uint8_t MT_RESUME = 2;
const uint8_t MT_SEEK = 3;
const uint8_t MT_GETELAPSED = 4;
const uint8_t MT_GETDURATION = 5;

typedef struct {
    char filename[128];
} Message_Play;
typedef struct {
    float timepoint;
} Message_Seek;

int main() {
    PlayThreadArgs ptargs;  
    int sock;

    /* AUDIO */
    {
        /* initialize SDL and SDL_mixer */
        SDL_Init(SDL_INIT_AUDIO);
        Mix_Init(0);

        /* Open 44.1khz, single-channel audio stream */
        if (Mix_OpenAudio(SAMPLE_RATE, AUDIO_S16SYS, 1, 1024) != 0) {
            printf("failed to initialize audio\n"); 
            return 1;
        }

        /* allocate one audio channel */
        Mix_AllocateChannels(1);

        ptargs.fd = -1;
        ptargs.chunk.allocated = 0;
        ptargs.chunk.abuf = (Uint8*) malloc(sizeof(int16_t) * CHUNK_SIZE);
        ptargs.chunk.volume = 128;
        ptargs.backbuf = malloc(sizeof(int16_t) * CHUNK_SIZE);
    }
    puts("initialized audio subsystem");

    /* NETWORK */
    {
        struct sockaddr_in addr;
        int opt = 1;

        /* create socket and check for valid descriptor */
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("failed to create socket");
            return 1;
        }
        /* allow subsequent launches to use the same address */
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        /* set up IPv4 address */
        addr.sin_family = AF_INET;
        addr.sin_port = htons(44);
        memset(&addr.sin_addr, 0, 4);
        /* bind to address and check for errors */
        if (bind(sock, (const struct sockaddr* const) &addr, sizeof(addr)) != 0) {
            perror("failed to bind socket");
            return 1;
        }
        /* listen for connections and check for errors */
        if (listen(sock, 1) != 0) {
            perror("socket failed to listen");
            return 1;
        }
    }
    puts("initialized network subsystem");
    puts("listening for requests...");

    /* listen for messages */
    for (;;) {
        /* accept incoming connection */
        int remote = accept(sock, NULL, NULL);
        uint8_t mtype;
        /* determine type of message (first byte) */
        read(remote, &mtype, 1);
        /* handle message type, reading additional data */
        if (mtype == MT_PLAY) {
            Message_Play mesg = {0};

            read(remote, &mesg, sizeof(mesg));
            printf("received play request: %s\n", mesg.filename);
            ptargs.fd = open(mesg.filename, O_RDONLY);  
            if (ptargs.fd > 0)  {
                struct stat st;
                float duration;

                fstat(ptargs.fd, &st);
                duration = ((float) st.st_size / sizeof(int16_t)) / SAMPLE_RATE;
                write(remote, &duration, 4);
                played_elapsed = 0;
                playthread_start(&ptargs);
            }
            else {
                printf("invalid file name: %s\n", mesg.filename);
            }
        }
        if (mtype == MT_PAUSE) {
            printf("received pause request\n");
            /* pause audio if audio is playing */
            if (playthread_active)
                Mix_Pause(0);
        }
        if (mtype == MT_RESUME) {
            printf("received resume request\n");
            /* resume audio if audio was playing */
            if (playthread_active)
                Mix_Resume(0);
        }
        if (mtype == MT_SEEK) {
            Message_Seek mesg;
            
            read(remote, &mesg, sizeof(mesg));
            if (ptargs.fd > 0) {
                struct stat st;
                uint32_t sample;
                int waspaused;

                fstat(ptargs.fd, &st);
                sample = (uint32_t) (mesg.timepoint * SAMPLE_RATE);
                if (sample >= st.st_size / sizeof(int16_t)) 
                    sample = st.st_size / sizeof(int16_t) - 1;
                printf("received seek request to sample %u of %lu\n", sample, st.st_size / 2);
                waspaused = Mix_Paused(0);
                Mix_Pause(0);
                lseek(ptargs.fd, sample * sizeof(int16_t), SEEK_SET);
                played_elapsed = (uint64_t) (1000000 * mesg.timepoint);
                if (!playthread_active)
                    playthread_start(&ptargs);
                Mix_Resume(0);
                if (Mix_Playing(0) || Mix_Paused(0))
                    Mix_HaltChannel(0);
                if (waspaused) {
                    while (!Mix_Playing(0));
                    Mix_Pause(0);
                }
            }
        }
        if (mtype == MT_GETELAPSED) {
            float elapsed = (float) played_elapsed / 1000000.0f;
            write(remote, &elapsed, 4);
        }
        if (mtype == MT_GETDURATION) {
            struct stat st;
            float duration;

            fstat(ptargs.fd, &st);
            duration = (float) st.st_size / sizeof(int16_t) / SAMPLE_RATE;
            write(remote, &duration, 4);
        }
        /* close remote connection */
        close(remote);
    }

    /* close audio file */
    if (ptargs.fd > 0) close(ptargs.fd);
    /* free memory */
    free(ptargs.chunk.abuf);
    free(ptargs.backbuf);
    /* close networking */
    close(sock);
    /* close audio subsystem and SDL */
    Mix_CloseAudio();
    Mix_Quit();
    SDL_Quit();

}
