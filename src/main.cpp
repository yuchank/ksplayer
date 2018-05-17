extern "C" {
  #include <libavformat/avformat.h>
  #include <libswresample/swresample.h>
  #include <libavutil/avstring.h>
  #include <libavutil/opt.h>
}

#include <SDL.h>
#include <SDL_thread.h>

#define SDL_AUDIO_BUFFER_SIZE 1024

typedef struct _PacketQueue {
  AVPacketList *first, *last;
  int nb_packets, size;
  SDL_mutex *mutex;
  SDL_cond *cond;
} PacketQueue;

typedef struct _VideoState {
  AVFormatContext *pFormatCtx;

  AVCodecContext *audioCtx;
  AVCodecContext *videoCtx;

  struct SwrContext *SwrCtx;

  int audio_index;
  int video_index;

  unsigned int audioBufSize;
  unsigned int audioBufIndex;

  AVStream *audio_stream;
  AVStream *video_stream;

  AVPacket *audio_packet;
  AVPacket *video_packet;

  AVFrame *audio_frame;

  PacketQueue audioq;
  PacketQueue videoq;

  SDL_Window *window;

//  SDL_Thread *parse_tid;

  char *filename;

} VideoState;

void packet_queue_init(PacketQueue *q) {
  memset(q, 0, sizeof(PacketQueue));
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCond();
}

int decode_audio_frame(VideoState *is) {

}

void audio_callback(void *userdata, uint8_t *stream, int len) {
  VideoState *is = static_cast<VideoState *>(userdata);
  fprintf(stderr, "len: %d\n", len);

//  while (len > 0) {
//    if (is->audioBufIndex >= is->audioBufSize) {
//
//    }
//  }
}

int stream_component_open(VideoState *is, int stream_index) {
  AVFormatContext *pFormatCtx = is->pFormatCtx;
  AVCodecParameters *codecpar;
  AVCodec *codec;
  AVCodecContext *codecCtx;

  SDL_AudioSpec wanted_spec = { 0 };
  SDL_AudioSpec audio_spec = { 0 };

  int rv;

  if (stream_index < 0 || stream_index >= pFormatCtx->nb_streams) {
    return -1;
  }

  codecpar = pFormatCtx->streams[stream_index]->codecpar;

  codec = avcodec_find_decoder(codecpar->codec_id);
  if (!codec) {
    return -1;
  }

  codecCtx = avcodec_alloc_context3(codec);
  if (!codecCtx) {
    return -1;
  }

  rv = avcodec_parameters_to_context(codecCtx, codecpar);
  if (rv < 0) {
    avcodec_free_context(&codecCtx);
    return rv;
  }

  rv = avcodec_open2(codecCtx, codec, NULL);
  if (rv < 0) {
    avcodec_free_context(&codecCtx);
    return rv;
  }

  if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
    is->audioCtx = codecCtx;
    is->audio_index = stream_index;
    is->audioBufSize = 0;
    is->audioBufIndex = 0;
    is->audio_stream = pFormatCtx->streams[stream_index];
    memset(&is->audio_packet, 0, sizeof(is->audio_packet));
    is->audio_frame = av_frame_alloc();
    if (!is->audio_frame) {
      return -1;
    }

    is->SwrCtx = swr_alloc();
    if (!is->SwrCtx) {
      return -1;
    }

    av_opt_set_channel_layout(is->SwrCtx, "in_channel_layout", codecCtx->channel_layout, 0);
    av_opt_set_channel_layout(is->SwrCtx, "out_channel_layout", codecCtx->channel_layout, 0);
    av_opt_set_int(is->SwrCtx, "in_sample_rate", codecCtx->sample_rate, 0);
    av_opt_set_int(is->SwrCtx, "out_sample_rate", codecCtx->sample_rate, 0);
    av_opt_set_sample_fmt(is->SwrCtx, "in_sample_fmt", codecCtx->sample_fmt, 0);
    av_opt_set_sample_fmt(is->SwrCtx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

    rv = swr_init(is->SwrCtx);
    if (rv < 0) {
      return rv;
    }

    wanted_spec.channels = codecCtx->channels;
    wanted_spec.freq = codecCtx->sample_rate;
    wanted_spec.format = AUDIO_F32;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.userdata = is;
    wanted_spec.callback = audio_callback;

    if (SDL_OpenAudio(&wanted_spec, &audio_spec) < 0) {
      fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
      return -1;
    }

    packet_queue_init(&is->audioq);

    SDL_PauseAudio(0);
  }
  else if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {

  }

  return 0;
}

int parse_thread(void *arg) {
//  VideoState *is = static_cast<VideoState *>(arg);
//  AVFormatContext *pFormatCtx;
//
//  int video_index;
//  int audio_index;
//
//  AVCodecParameters *codecpar;
//
//  SDL_Window *window = is->window;
//
//  is->pFormatCtx = avformat_alloc_context();
//  pFormatCtx = is->pFormatCtx;
//
//  if (avformat_open_input(&pFormatCtx, is->filename, NULL, NULL)) {
//    return -1;
//  }
//
//  is->pFormatCtx = pFormatCtx;
//
//  if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
//    return -1;
//  }
//
//  av_dump_format(pFormatCtx, 0, is->filename, 0);
//
//  video_index = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
//  audio_index = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, video_index, NULL, 0);
//
//  if (audio_index >= 0) {
//    stream_component_open(is, audio_index);
//  }
//  if (video_index >= 0) {
//    codecpar = pFormatCtx->streams[video_index]->codecpar;
//    SDL_SetWindowSize(window, codecpar->width, codecpar->height);
//    stream_component_open(is, video_index);
//  }

  return 0;
}

int main(int argc, char *argv[])
{
  SDL_Event event;
  SDL_Window *window = NULL;

  VideoState *is = NULL;

  AVFormatContext *pFormatCtx;
  AVCodecParameters *codecpar;

  int video_index;
  int audio_index;

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

//  is->parse_tid = SDL_CreateThread(parse_thread, "parse", is);
//  if (!is->parse_tid) {
//    goto exit;
//  }

  pFormatCtx = avformat_alloc_context();
  is->pFormatCtx = pFormatCtx;

  if (avformat_open_input(&pFormatCtx, is->filename, NULL, NULL)) {
    goto exit;
  }

  if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
    goto exit;
  }

  av_dump_format(pFormatCtx, 0, is->filename, 0);

  video_index = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  audio_index = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, video_index, NULL, 0);

  if (audio_index >= 0) {
    if (stream_component_open(is, audio_index) < 0) {
      goto exit;
    }
  }
  if (video_index >= 0) {
    codecpar = pFormatCtx->streams[video_index]->codecpar;
    SDL_SetWindowSize(window, codecpar->width, codecpar->height);
    if (stream_component_open(is, video_index) < 0) {
      goto exit;
    }
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

  if (is->SwrCtx) {
    swr_free(&is->SwrCtx);
  }

  if (is->audio_frame) {
    av_frame_free(&is->audio_frame);
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
