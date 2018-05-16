extern "C" {
  #include <libavformat/avformat.h>
}

#include <SDL.h>
#include <SDL_thread.h>

typedef struct _VideoState {

  SDL_Window *window;

  SDL_Thread *parse_tid;

} VideoState;

int parse_thread(void *arg) {

}

int main(int argc, char *argv[])
{
  SDL_Event event;
  SDL_Window *window = NULL;

  VideoState *is = NULL;

  if (argc < 2) {
    fprintf(stderr, "Usage: ksplayer <file>\n");
    exit(1);
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
    exit(1);
  }

  is = static_cast<VideoState *>(av_mallocz(sizeof(VideoState)));

  window = SDL_CreateWindow("SDL_CreateTexture", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_RESIZABLE);
  if (!window) {
    goto exit;
  }
  is->window = window;

  is->parse_tid = SDL_CreateThread(parse_thread, "parse", is);
  if (!is->parse_tid) {
    goto exit;
  }

  while (true) {
    SDL_PollEvent(&event);
    switch (event.type) {
      case SDL_QUIT:
        goto exit;
      default:
        break;
    }
  }

exit:
  if (window) {
    SDL_DestroyWindow(window);
  }

  if (is) {
    av_free(is);
  }

  SDL_Quit();

  return 0;
}
