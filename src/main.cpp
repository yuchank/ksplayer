extern "C" {
  #include <libavformat/avformat.h>
  #include <libswscale/swscale.h>
  #include <libswresample/swresample.h>
  #include <libavutil/avstring.h>
  #include <libavutil/imgutils.h>
}

#include <SDL.h>

#define FF_QUIT_EVENT (SDL_USEREVENT + 2)

#define MAX_QUEUE_SIZE (15 * 1024 * 1024)

#define SDL_AUDIO_BUFFER_SIZE 1024

#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000

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
  // common
  AVFormatContext *p_format_context;
  char *p_filename;
  int quit;

  // video
  AVCodecContext *p_video_codec_context;
  AVStream *p_video_stream;
  PacketQueue videoq;
  AVPacket video_packet;
  struct SwsContext *p_sws_context;
  int video_index;
  AVFrame *p_frame_RGB;
  uint8_t *p_frame_buffer;
  SDL_Thread *p_parser_tid;
  SDL_Thread *p_video_tid;
  SDL_Window *p_window;
  SDL_Renderer *p_renderer;

  // audio
  AVCodecContext *p_audio_codec_context;
  PacketQueue audioq;
  AVPacket audio_packet;
  struct SwrContext *p_swr_context;
  AVFrame *p_audio_frame;
  int audio_index;
  unsigned int audio_buf_size, audio_buf_index;
  uint8_t audio_buffer[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
  uint8_t audio_converted_data[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
  int has_audio_frames;
} VideoState;

VideoState *p_global_video_state;

void packet_queue_init(PacketQueue *q)
{
  memset(q, 0, sizeof(PacketQueue));
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *pq, const AVPacket *p_src_pkt)
{
  AVPacketList *p_packet_list;
  AVPacket pkt;
  int rv;

  if (!pq) return -1;
  rv = av_packet_ref(&pkt, p_src_pkt);
  if (rv) return rv;
  p_packet_list = (AVPacketList *)av_malloc(sizeof(AVPacketList));
  if (!p_packet_list) return -1;
  p_packet_list->pkt = pkt;
  p_packet_list->next = NULL;

  SDL_LockMutex(pq->mutex);

  if (!pq->last)
    pq->first = p_packet_list;
  else
    pq->last->next = p_packet_list;
  pq->last = p_packet_list;
  pq->nb_packets++;
  pq->size += p_packet_list->pkt.size;
  SDL_CondSignal(pq->cond);

  SDL_UnlockMutex(pq->mutex);
  return 0;
}

int packet_queue_get(PacketQueue *pq, AVPacket *pkt, int block)
{
  AVPacketList *p_packet_list;
  int rv;

  if (!pq || !pkt) return -1;

  SDL_LockMutex(pq->mutex);

  while (true) {
    if (p_global_video_state->quit) {
      rv = -1;
      break;
    }
    p_packet_list = pq->first;
    if (p_packet_list) {
      pq->first = p_packet_list->next;
      if (!pq->first)
        pq->last = NULL;
      pq->nb_packets--;
      pq->size -= p_packet_list->pkt.size;
      *pkt = p_packet_list->pkt;
      av_free(p_packet_list);
      rv = 1;
      break;
    }
    else if (!block) {
      rv = 0;
      break;
    }
    else  {
      SDL_CondWait(pq->cond, pq->mutex);
    }
  }
  SDL_CondSignal(pq->cond);

  SDL_UnlockMutex(pq->mutex);

  return rv;
}

int decode_audio_frame(VideoState *is)
{
  int data_size = 0, out_size = 0, rv, has_packet = 0;
  uint8_t *p_converted = &is->audio_converted_data[0];

  while (true) {
    while (is->has_audio_frames) {
      rv = avcodec_receive_frame(is->p_audio_codec_context, is->p_audio_frame);
      if (rv) {
        is->has_audio_frames = 0;
        break;
      }

      av_samples_get_buffer_size(NULL, is->p_audio_codec_context->channels, is->p_audio_frame->nb_samples, is->p_audio_codec_context->sample_fmt, 1);
      out_size = av_samples_get_buffer_size(NULL, is->p_audio_codec_context->channels, is->p_audio_frame->nb_samples, AV_SAMPLE_FMT_FLT, 1);
      swr_convert(is->p_swr_context, &p_converted, is->p_audio_frame->nb_samples, (const uint8_t**)&is->p_audio_frame->data[0], is->p_audio_frame->nb_samples);
      memcpy(is->audio_buffer, p_converted, out_size);
      data_size = out_size;

      /* We have data, return it and come back for more later */
      return data_size;
    }

    if (has_packet) {

    }

    if (is->quit) return -1;

    if (packet_queue_get(&is->audioq, &is->audio_packet, 1) < 0)
      return -1;

    has_packet = 1;

    rv = avcodec_send_packet(is->p_audio_codec_context, &is->audio_packet);
    if (rv) return rv;

    is->has_audio_frames = 1;
  }

  return -1;
}

void audio_callback(void *data, uint8_t *stream, int len)
{
  auto *is = static_cast<VideoState *>(data);
  int audio_size;

  while (len > 0) {   // 8192
    if (is->audio_buf_index >= is->audio_buf_size) {
      // already sent all data; get more
      audio_size = decode_audio_frame(is);
      if (audio_size < 0) {
        // error
        is->audio_buf_size = SDL_AUDIO_BUFFER_SIZE;   // 1024
        memset(is->audio_buffer, 0, sizeof(is->audio_buffer));
      }
      else {
        is->audio_buf_size = audio_size;
      }
      is->audio_buf_index = 0;
    }
  }
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

    wanted_spec.channels = static_cast<Uint8 >(p_codec_context->channels);
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
  AVPacket pkt;
  int rv;

  while (true) {
    if (is->quit) break;

    if (is->videoq.size >= MAX_QUEUE_SIZE || is->audioq.size >= MAX_QUEUE_SIZE) {
      SDL_Delay(10);
      continue;
    }

    rv = av_read_frame(is->p_format_context, &pkt);
    if (rv < 0) break;

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
