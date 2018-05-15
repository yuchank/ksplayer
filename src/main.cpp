extern "C" {
  #include <libavformat/avformat.h>
}

#include <SDL.h>
#include <SDL_thread.h>

int main(int argc, char *argv[]) {

  SDL_Init(SDL_INIT_VIDEO);

  fprintf(stderr, "ERROR: %d\n", 3);
  SDL_Log("sdl_log available?");
  av_log(NULL, AV_LOG_INFO, "av_log available?");

  SDL_Window *window = SDL_CreateWindow("SDL_CreateTexture",
                                        SDL_WINDOWPOS_UNDEFINED,
                                        SDL_WINDOWPOS_UNDEFINED,
                                        640,
                                        480,
                                        SDL_WINDOW_RESIZABLE);

  SDL_Event event;
  while (1) {
    SDL_PollEvent(&event);
    if (event.type == SDL_QUIT) {
      break;
    }
  }

  SDL_Quit();

  return 0;
}
