extern "C" {
  #include <libavformat/avformat.h>
  #include <libswresample/swresample.h>
  #include <libavutil/avstring.h>
  #include <libavutil/opt.h>
}

#include <SDL.h>
#include <SDL_thread.h>

#define FF_QUIT_EVENT (SDL_USEREVENT + 2)

#define MAX_QUEUE_SIZE (15 * 1024 * 1024)   // 15M

#define SDL_AUDIO_BUFFER_SIZE 1024

#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000

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

  uint8_t audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
  uint8_t audio_converted_data[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];

  unsigned int audioBufSize;
  unsigned int audioBufIndex;

  AVStream *audio_stream;
  AVStream *video_stream;

  AVPacket audio_packet;
  AVPacket video_packet;

  int has_audio_frame;

  AVFrame *audio_frame;

  PacketQueue audioq;
  PacketQueue videoq;

  SDL_Window *window;

  SDL_Thread *parse_tid;

  char *filename;
  int quit;

} VideoState;

void packet_queue_init(PacketQueue *q) {
  memset(q, 0, sizeof(PacketQueue));
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *q, const AVPacket *src_pkt) {
  AVPacketList *packet_list;
  AVPacket packet;
  int rv;

  if (!q) {
    return -1;
  }

  rv = av_packet_ref(&packet, src_pkt);
  if (rv) {
    return rv;
  }

  packet_list = (AVPacketList *)av_malloc(sizeof(AVPacketList));
  if (!packet_list) {
    return -1;
  }
  packet_list->pkt = packet;
  packet_list->next = NULL;

  SDL_LockMutex(q->mutex);
  if (!q->last) {
    q->first = packet_list;
  }
  else {
    q->last->next = packet_list;
  }
  q->last = packet_list;
  q->nb_packets++;
  q->size += packet_list->pkt.size;
  SDL_CondSignal(q->cond);
  SDL_UnlockMutex(q->mutex);

  return 0;
}

int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block) {
  AVPacketList *packet_list;
  int rv;

  if (!q || !pkt) {
    return -1;
  }
  SDL_LockMutex(q->mutex);
  while (true) {
    packet_list = q->first;
    if (packet_list) {
      q->first = packet_list->next;
      if (!q->first) {
        q->last = NULL;
      }
      q->nb_packets--;
      q->size -= packet_list->pkt.size;
      *pkt = packet_list->pkt;
      av_free(packet_list);
      rv = 1;
      break;
    }
    else if (!block) {
      rv = 0;
      break;
    }
    else {
      SDL_CondWait(q->cond, q->mutex);
    }
  }
  SDL_UnlockMutex(q->mutex);
  return rv;
}

int decode_audio_frame(VideoState *is) {
  int has_packet = 0;
  int data_size = 0;
  int out_size = 0;
  int len2;
  int rv;
  uint8_t *converted = &is->audio_converted_data[0];

  while(true) {
    while (is->has_audio_frame) {
      rv = avcodec_receive_frame(is->audioCtx, is->audio_frame);
      if (rv) {
        is->has_audio_frame = 0;
        break;
      }

      data_size = av_samples_get_buffer_size(NULL, is->audioCtx->channels, is->audio_frame->nb_samples, is->audioCtx->sample_fmt, 1);
      out_size = av_samples_get_buffer_size(NULL, is->audioCtx->channels, is->audio_frame->nb_samples, AV_SAMPLE_FMT_FLT, 1);
      len2 = swr_convert(is->SwrCtx, &converted, is->audio_frame->nb_samples, (const uint8_t **)&is->audio_frame->data[0], is->audio_frame->nb_samples);
      memcpy(is->audio_buf, converted, out_size);
      data_size = out_size;

      // we have data, return it and come back for more later
      return data_size;
    }

    if (has_packet) {
      av_packet_unref(&is->audio_packet);
    }

    if (is->quit) {
      return -1;
    }

    if (packet_queue_get(&is->audioq, &is->audio_packet, 1) < 0) {
      return -1;
    }

    has_packet = 1;

    rv = avcodec_send_packet(is->audioCtx, &is->audio_packet);
    if (rv) {
      return rv;
    }

    is->has_audio_frame = 1;
  }

  return -1;
}

void audio_callback(void *userdata, uint8_t *stream, int len) {
  VideoState *is = static_cast<VideoState *>(userdata);
  int audio_size;
  int len1;

  while (len > 0) {
    if (is->audioBufIndex >= is->audioBufSize) {
      audio_size = decode_audio_frame(is);
      if (audio_size < 0) {
        // error
        is->audioBufSize = SDL_AUDIO_BUFFER_SIZE;
        memset(is->audio_buf, 0, sizeof(is->audio_buf));
      }
      else {
        is->audioBufSize = audio_size;
      }
      is->audioBufIndex = 0;
    }
    len1 = is->audioBufSize - is->audioBufIndex;
    if (len1 > len) {
      len1 = len;
    }
    memcpy(stream, (uint8_t *)is->audio_buf + is->audioBufIndex, len1);
    len -= len1;
    stream += len1;
    is->audioBufIndex += len1;
  }
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
  VideoState *is = static_cast<VideoState *>(arg);
  AVPacket pkt;
  int rv;

  while (true) {
    if (is->quit) {
      break;
    }

    if (is->videoq.size >= MAX_QUEUE_SIZE || is->audioq.size >= MAX_QUEUE_SIZE) {
      SDL_Delay(10);
      continue;
    }

    rv = av_read_frame(is->pFormatCtx, &pkt);
    if (rv < 0) {
      break;
    }

    if (pkt.stream_index == is->audio_index) {
      packet_queue_put(&is->audioq, &pkt);
    }
    else if (pkt.stream_index == is->video_index) {
      packet_queue_put(&is->videoq, &pkt);
    }
    av_packet_unref(&pkt);
  }

  while (!is->quit) {
    SDL_Delay(100);
  }

  SDL_Event event;
  event.type = FF_QUIT_EVENT;
  event.user.data1 = is;
  SDL_PushEvent(&event);

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

  is->parse_tid = SDL_CreateThread(parse_thread, "parse", is);
  if (!is->parse_tid) {
    goto exit;
  }

  while (true) {
    SDL_PollEvent(&event);
    switch (event.type) {
      case FF_QUIT_EVENT:
      case SDL_QUIT:
        is->quit = 1;
        SDL_CondSignal(is->audioq.cond);
        SDL_Quit();
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

  return 0;
}
