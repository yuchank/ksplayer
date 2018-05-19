extern "C" {
  #include <libavformat/avformat.h>
  #include <libswscale/swscale.h>
  #include <libavutil/avstring.h>
  #include <libavutil/imgutils.h>
}

#include <SDL.h>

#define SDL_AUDIO_BUFFER_SIZE 1024

typedef struct _PacketQueue {
  AVPacketList *first, *last;
  int nb_packets, size;
  SDL_mutex *mutex;
  SDL_cond *cond;
} PacketQueue;

typedef struct _VideoPicture {
  SDL_Texture *texture;
  int width, height;
  int allocated;
  double pts;
} VideoPicture;

typedef struct _VideoState {
  AVFormatContext *p_format_context;

  AVCodecContext *p_audio_codec_context;
  AVCodecContext *p_video_codec_context;

  struct SwsContext *p_sws_context;

  int audio_index;
  int video_index;

  AVStream *p_video_stream;
  PacketQueue videoq;

  AVFrame *p_frame_RGB;
  uint8_t *p_frame_buffer;

  SDL_Thread *p_parser_tid;
  SDL_Thread *p_video_tid;

  SDL_Window *p_window;
  SDL_Renderer *p_renderer;

  char *p_filename;
  int quit;
} VideoState;

void packet_queue_init(PacketQueue *q)
{
  memset(q, 0, sizeof(PacketQueue));
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCond();
}

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
  AVCodecParameters *p_codec_par = is->p_format_context->streams[stream_index]->codecpar;
  AVCodec *p_codec;
  AVCodecContext *p_codec_context;
  SDL_AudioSpec wanted_spec = { 0 }, audio_spec = { 0 };
  int rv, size;

  if (stream_index < 0 || stream_index >= is->p_format_context->nb_streams)
    return -1;

  p_codec = avcodec_find_decoder(p_codec_par->codec_id);
  if (!p_codec) return -1;

  p_codec_context = avcodec_alloc_context3(p_codec);
  if (!p_codec_context) return -1;

  rv = avcodec_parameters_to_context(p_codec_context, p_codec_par);
  if (rv < 0) {
    avcodec_free_context(&p_codec_context);
    return rv;
  }

  rv = avcodec_open2(p_codec_context, p_codec, nullptr);
  if (rv < 0) {
    avcodec_free_context(&p_codec_context);
    return rv;
  }

  if (p_codec_par->codec_type == AVMEDIA_TYPE_AUDIO) {
    is->p_audio_codec_context = p_codec_context;

    wanted_spec.channels = p_codec_context->channels;
    wanted_spec.freq = p_codec_context->sample_rate;
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
  else if (p_codec_par->codec_type == AVMEDIA_TYPE_VIDEO) {
    is->p_video_codec_context = p_codec_context;
    is->p_video_stream = is->p_format_context->streams[stream_index];

    is->p_sws_context = sws_getContext(p_codec_context->width, p_codec_context->height, p_codec_context->pix_fmt, p_codec_context->width, p_codec_context->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!is->p_sws_context) return -1;

    is->p_frame_RGB = av_frame_alloc();
    if (!is->p_frame_RGB) return -1;

    size = av_image_get_buffer_size(AV_PIX_FMT_RGB24, p_codec_par->width, p_codec_par->height, 8);
    if (size < 1) return -1;

    is->p_frame_buffer = (uint8_t*)av_malloc(size);
    if (!is->p_frame_buffer) return AVERROR(ENOMEM);

    rv = av_image_fill_arrays(&is->p_frame_RGB->data[0], &is->p_frame_RGB->linesize[0], is->p_frame_buffer, AV_PIX_FMT_RGB24, p_codec_par->width, p_codec_par->height, 1);
    if (rv < 0) return rv;

    packet_queue_init(&is->videoq);
    is->p_video_tid = SDL_CreateThread(video_thread, "video", is);
    if (!is->p_video_tid) return -1;
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

  is->p_filename = static_cast<char *>(av_mallocz(strlen(argv[1]) + 1));
  av_strlcpy(is->p_filename, argv[1], strlen(argv[1]) + 1);

  is->p_format_context = avformat_alloc_context();;
  if (!is->p_format_context) goto cleanup;

  rv = avformat_open_input(&is->p_format_context, is->p_filename, nullptr, nullptr);
  if (rv < 0) goto cleanup;

  rv = avformat_find_stream_info(is->p_format_context, nullptr);
  if (rv < 0) goto cleanup;

  av_dump_format(is->p_format_context, 0, is->p_filename, 0);

  is->video_index = av_find_best_stream(is->p_format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  is->audio_index = av_find_best_stream(is->p_format_context, AVMEDIA_TYPE_AUDIO, -1, is->video_index, nullptr, 0);
  if (is->video_index < 0 && is->audio_index < 0) {
    rv = -1;
    goto cleanup;
  }

  is->p_window = SDL_CreateWindow(is->p_filename, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, 0);
  if (!is->p_window) goto cleanup;

  is->p_renderer = SDL_CreateRenderer(is->p_window, -1, 0);
  if (!is->p_renderer) goto cleanup;

  if (is->audio_index >= 0) {
    rv = stream_component_open(is, is->audio_index);
    if (rv < 0) goto cleanup;
  }
  if (is->video_index >= 0) {
    AVCodecParameters *codec_par = is->p_format_context->streams[is->video_index]->codecpar;
    SDL_SetWindowSize(is->p_window, codec_par->width, codec_par->height);
    rv = stream_component_open(is, is->video_index);
    if (rv < 0) goto cleanup;
  }

  is->p_parser_tid = SDL_CreateThread(parse_thread, "parse", is);
  if (!is->p_parser_tid) goto cleanup;

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
  if (is->p_frame_RGB) av_frame_free(&is->p_frame_RGB);
  if (is->p_frame_buffer) av_free(is->p_frame_buffer);
  if (is->p_format_context) {
    avformat_close_input(&is->p_format_context);
    avformat_free_context(is->p_format_context);
  }
  if (is->p_filename) av_free(is->p_filename);
  if (is) av_free(is);
  return rv;
}
