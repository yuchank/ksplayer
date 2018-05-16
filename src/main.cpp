extern "C" {
  #include <libavformat/avformat.h>
  #include <libavutil/avstring.h>
}

#include <SDL.h>
#include <SDL_thread.h>

typedef struct _VideoState {
  AVFormatContext *pFormatCtx;

  int audio_index;
  int video_index;

  SDL_Window *window;

  SDL_Thread *parse_tid;

  char *filename;

} VideoState;

int stream_component_open(VideoState *is, int stream_index) {
  AVFormatContext *pFormatCtx = is->pFormatCtx;

  return 0;
}

int parse_thread(void *arg) {
  VideoState *is = static_cast<VideoState *>(arg);
  AVFormatContext *pFormatCtx;

  int video_index;
  int audio_index;

  AVCodecParameters *codecpar;

  SDL_Window *window = is->window;

  is->pFormatCtx = avformat_alloc_context();
  pFormatCtx = is->pFormatCtx;

  if (avformat_open_input(&pFormatCtx, is->filename, NULL, NULL)) {
    return -1;
  }

  is->pFormatCtx = pFormatCtx;

  if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
    return -1;
  }

  av_dump_format(pFormatCtx, 0, is->filename, 0);

  video_index = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  audio_index = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, video_index, NULL, 0);

  if (audio_index >= 0) {
    stream_component_open(is, audio_index);
  }
  if (video_index >= 0) {
    codecpar = pFormatCtx->streams[video_index]->codecpar;
    SDL_SetWindowSize(window, codecpar->width, codecpar->height);
    stream_component_open(is, video_index);
  }

  return 0;
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

  is->filename = static_cast<char *>(av_mallocz(strlen(argv[1]) + 1));
  av_strlcpy(is->filename, argv[1], strlen(argv[1]) + 1);

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
  if (is->filename) {
    av_free(is->filename);
  }

  if (is->pFormatCtx) {
    avformat_close_input(&is->pFormatCtx);
    avformat_free_context(is->pFormatCtx);
  }

  if (window) {
    SDL_DestroyWindow(window);
  }

  if (is) {
    av_free(is);
  }

  SDL_Quit();

  return 0;
}
