extern "C" {
  #include <libavformat/avformat.h>
  #include <libavutil/avstring.h>
}

#include <SDL.h>

#define SDL_AUDIO_BUFFER_SIZE 1024

typedef struct _VideoState {
  AVFormatContext *format_context;

  int audio_index;
  int video_index;

  AVCodecContext *audio_codec_context;
  AVCodecContext *video_codec_context;

  SDL_Thread *parser_tid;
  SDL_Thread *video_tid;

  SDL_Window *window;
  SDL_Renderer *renderer;

  char *filename;
  int quit;
} VideoState;

void audio_callback(void *data, uint8_t *stream, int len)
{

}

int video_thread(void *data)
{
  auto *is = static_cast<VideoState *>(data);

  return 0;
}

int stream_component_open(VideoState *is, int stream_index)
{
  AVCodecParameters *codec_par = is->format_context->streams[stream_index]->codecpar;
  AVCodec *codec;
  AVCodecContext *codec_context;
  SDL_AudioSpec wanted_spec = { 0 }, audio_spec = { 0 };
  int rv;

  if (stream_index < 0 || stream_index >= is->format_context->nb_streams)
    return -1;

  codec = avcodec_find_decoder(codec_par->codec_id);
  if (!codec) return -1;

  codec_context = avcodec_alloc_context3(codec);
  if (!codec_context) return -1;

  rv = avcodec_parameters_to_context(codec_context, codec_par);
  if (rv < 0) {
    avcodec_free_context(&codec_context);
    return rv;
  }

  rv = avcodec_open2(codec_context, codec, nullptr);
  if (rv < 0) {
    avcodec_free_context(&codec_context);
    return rv;
  }

  if (codec_par->codec_type == AVMEDIA_TYPE_AUDIO) {
    is->audio_codec_context = codec_context;

    wanted_spec.channels = codec_context->channels;
    wanted_spec.freq = codec_context->sample_rate;
    wanted_spec.format = AUDIO_F32;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.userdata = is;
    wanted_spec.callback = audio_callback;

    if (SDL_OpenAudio(&wanted_spec, &audio_spec) < 0) {
      fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
      return -1;
    }

    SDL_PauseAudio(0);
  }
  else if (codec_par->codec_type == AVMEDIA_TYPE_VIDEO) {
    is->video_tid = SDL_CreateThread(video_thread, "video", is);
  }

  return 0;
}

int parse_thread(void *data)
{
  auto *is = static_cast<VideoState *>(data);

  for (;;) {
    if (is->quit) break;
  }

  return 0;
}

int main(int argc, char *argv[])
{
  int rv = 0;
  SDL_Event evt;
  VideoState *is = nullptr;

  if (argc < 2) {
    fprintf(stderr, "no filename\n");
    return rv;
  }

  if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
    fprintf(stderr, "Unable to init SDL: %s \n", SDL_GetError());
    goto cleanup;
  }

  is = static_cast<VideoState *>(av_mallocz(sizeof(VideoState)));
  if (!is) goto cleanup;

  is->filename = static_cast<char *>(av_mallocz(strlen(argv[1]) + 1));
  av_strlcpy(is->filename, argv[1], strlen(argv[1]) + 1);

  is->format_context = avformat_alloc_context();;
  if (!is->format_context) goto cleanup;

  rv = avformat_open_input(&is->format_context, is->filename, nullptr, nullptr);
  if (rv < 0) goto cleanup;

  rv = avformat_find_stream_info(is->format_context, nullptr);
  if (rv < 0) goto cleanup;

  av_dump_format(is->format_context, 0, is->filename, 0);

  is->video_index = av_find_best_stream(is->format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  is->audio_index = av_find_best_stream(is->format_context, AVMEDIA_TYPE_AUDIO, -1, is->video_index, nullptr, 0);
  if (is->video_index < 0 && is->audio_index < 0) {
    rv = -1;
    goto cleanup;
  }

  is->window = SDL_CreateWindow(is->filename, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, 0);
  if (!is->window) goto cleanup;

  is->renderer = SDL_CreateRenderer(is->window, -1, 0);
  if (!is->renderer) goto cleanup;

  if (is->audio_index >= 0) {
    rv = stream_component_open(is, is->audio_index);
    if (rv < 0) goto cleanup;
  }
  if (is->video_index >= 0) {
    AVCodecParameters *codec_par = is->format_context->streams[is->video_index]->codecpar;
    SDL_SetWindowSize(is->window, codec_par->width, codec_par->height);
    rv = stream_component_open(is, is->video_index);
    if (rv < 0) goto cleanup;
  }

  is->parser_tid = SDL_CreateThread(parse_thread, "parse", is);
  if (!is->parser_tid) goto cleanup;

  for (;;) {
    SDL_WaitEvent(&evt);
    switch (evt.type)
    {
      case SDL_QUIT:
        is->quit = 1;
        SDL_Quit();
        goto cleanup;
      default:
        break;
    }
  }

  cleanup:
  if (is->format_context) {
    avformat_close_input(&is->format_context);
    avformat_free_context(is->format_context);
  }
  if (is->filename) av_free(is->filename);
  if (is) av_free(is);
  return rv;
}
