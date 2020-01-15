
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

/* audio playback constants */
const int SAMPLE_RATE = 44100;
const int CHUNK_SIZE = SAMPLE_RATE * 2;

/*
 * PLAY THREAD
 */

/* playthread variables */
int playthread_active = 0;
pthread_t playthread;
uint64_t played_elapsed = 0;

/* arguments to send to audio play thread */
typedef struct {
    int         fd;      /* streaming file descriptor      */
    Mix_Chunk   chunk;   /* frontbuffer and playable chunk */
    int16_t*    backbuf; /* backbuffer samples             */
} PlayThreadArgs;

void* playthread_func(void* pargs) {
    PlayThreadArgs* args = (PlayThreadArgs*) pargs;

    /* 
     * loop while thread is marked active 
     * allows killing thread externally and gracefully 
     */
    while (playthread_active) {
        int16_t* oldbuf;
        int numread;

        /* read next chunk and store number of bytes read (in case 0 or less than chunk size) */
        numread = read(args->fd, args->backbuf, CHUNK_SIZE * sizeof(int16_t));
        /* kill thread on EOF, IO error, or inactive thread */
        if (!playthread_active || numread <= 0) break;
        /* swap front/back buffers and set played chunk size */
        oldbuf = (int16_t*) args->chunk.abuf;
        args->chunk.alen = numread;
        args->chunk.abuf = (Uint8*) args->backbuf;
        args->backbuf = oldbuf;

        /* play front buffer */
        Mix_PlayChannel(0, &args->chunk, 0);
        /* wait for clip to finish */
        while (playthread_active && (Mix_Paused(0) || Mix_Playing(0))) {
            usleep(10000);
            /* accumulate time if audio active */
            if (!Mix_Paused(0) && Mix_Playing(0))
                played_elapsed += 10000;
        }
    } 
    playthread_active = 0;

    /* thread returns nothing */
    return NULL;
}

/* helper function to stop thread */
void playthread_stop() {
    if (playthread_active) {
        /* mark thread as inactive and wait to exit */
        playthread_active = 0;
        pthread_join(playthread, NULL);
    }
}

/* helper function to start thread */
void playthread_start(PlayThreadArgs* args) {
    /* stop thread if active */
    if (playthread_active) playthread_stop(); 
    /* mark thread as active and create */
    playthread_active = 1;
    pthread_create(&playthread, NULL, playthread_func, args);
}

/*
 * MESSAGE FORMAT
 */

/* message types */
const uint8_t MT_PLAY        = 0;
const uint8_t MT_PAUSE       = 1;
const uint8_t MT_RESUME      = 2;
const uint8_t MT_SEEK        = 3;
const uint8_t MT_GETELAPSED  = 4;
const uint8_t MT_GETDURATION = 5;

/* play request fields */
typedef struct {
    /* file name string, max length of 128 */
    char filename[128];
} Message_Play;
/* seek request fields */
typedef struct {
    /* seek point in seconds, dword float */
    float timepoint;
} Message_Seek;

/* entry point */
int main() {
    /* forward-declare playthread arguments and socket file descriptor */
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

        /* initialize playthread arguments */
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

        /* handle play request */
        if (mtype == MT_PLAY) {
            /* declare zero-initialized play request struct */
            Message_Play mesg = {0};

            /* read play request struct from remote socket */
            read(remote, &mesg, sizeof(mesg));
            printf("received play request: %s\n", mesg.filename);

            /* attempt to open requested file */
            ptargs.fd = open(mesg.filename, O_RDONLY);  
            
            /* handle valid file */
            if (ptargs.fd > 0)  {
                /* start play thread at time point 0 */
                played_elapsed = 0;
                playthread_start(&ptargs);
            }
            else {
                /* print error on invalid file name */
                printf("invalid file name: %s\n", mesg.filename);
            }
        }

        /* handle pause request */
        if (mtype == MT_PAUSE) {
            printf("received pause request\n");
            /* pause audio if audio is playing */
            if (playthread_active)
                Mix_Pause(0);
        }
        
        /* handle resume request */
        if (mtype == MT_RESUME) {
            printf("received resume request\n");
            /* resume audio if audio was playing */
            if (playthread_active)
                Mix_Resume(0);
        }
        
        /* handle seek request */
        if (mtype == MT_SEEK) {
            /* zero-initialize seek request struct */
            Message_Seek mesg = {0};
            
            /* read request struct from remote socket */
            read(remote, &mesg, sizeof(mesg));

            /* if current file descriptor is valid */
            if (ptargs.fd > 0) {
                struct stat st;
                uint32_t sample;
                int waspaused;

                /* get file size, compute sample */
                fstat(ptargs.fd, &st);
                sample = (uint32_t) (mesg.timepoint * SAMPLE_RATE);
                if (sample >= st.st_size / sizeof(int16_t)) 
                    sample = st.st_size / sizeof(int16_t) - 1;
                printf("received seek request to sample %u of %lu\n", sample, st.st_size / 2);
                /* save current paused state, then force-pause stream */
                waspaused = Mix_Paused(0);
                Mix_Pause(0);
                /* seek to correct byte in streaming file */
                lseek(ptargs.fd, sample * sizeof(int16_t), SEEK_SET);
                /* change elapsed time to requested time */
                played_elapsed = (uint64_t) (1000000 * mesg.timepoint);
                
                /* start thread if inactive and resume play */
                if (!playthread_active)
                    playthread_start(&ptargs);
                Mix_Resume(0);

                /* halt channel to prevent waiting */
                if (Mix_Playing(0) || Mix_Paused(0)) Mix_HaltChannel(0);

                /* pause mix if it was playing before seeking */
                if (waspaused) {
                    while (!Mix_Playing(0));
                    Mix_Pause(0);
                }
            }
        }
        
        /* handle elapsed time query */
        if (mtype == MT_GETELAPSED) {
            /* compute elapsed time in seconds, send as dword float */
            float elapsed = (float) played_elapsed / 1000000.0f;
            write(remote, &elapsed, 4);
        }

        /* handle duration query */
        if (mtype == MT_GETDURATION) {
            struct stat st;
            float duration;

            /* determine/send duration based on sample rate, sample size, and file size */
            fstat(ptargs.fd, &st);
            duration = (float) st.st_size / sizeof(int16_t) / SAMPLE_RATE;
            write(remote, &duration, 4);
        }
        /* close remote conn/send ction */
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
