extern "C" {
  #include <libavformat/avformat.h>
  #include <libswscale/swscale.h>
  #include <libswresample/swresample.h>
  #include <libavutil/avstring.h>
  #include <libavutil/imgutils.h>
}

#include <SDL.h>

typedef struct VideoState {
  SDL_Thread *read_tid;   // 204

  char *filename;         // 291
} VideoState;

/* options specified by the user */
static AVInputFormat *file_iformat;   // 310
static const char *input_filename;    // 311

static int decoder_decode_frame(Decoder *d, AVFrame *frame, AVSubtitle *sub)    // 585
{
  while (true) {
    AVPacket pkt;
    if (d->queue->serial == d->pkt_serial) {
      do {
        switch (d->avctx->codec_type) {
          case AVMEDIA_TYPE_VIDEO:
            // 6-3. decode send-receive pair
            ret = avcodec_receive_frame(d->avctx, frame);
            break;
        }
      } while (ret != AVERROR(EAGAIN));
    }

    if (pkt.data == flush_pkt.data) {

    }
    else {
      if (d->avctx-codec_type == AVMEDIA_TYPE_SUBTITLE) {

      }
      else {
        // 6-2. decode send-receive pair
        if (avcodec_send_packet(d->avctx, &pkt) == AVERROR(EAGAIN)) {

        }
      }
    }
  }
}

static void frame_queue_push(FrameQueue *f)       // 772
{

}

static void video_image_display(VideoState *is)   // 957
{

}

static void video_audio_display(VideoState *is)   // 1043
{

}

static void stream_close(VideoState *is)          // 1242
{

}

/* display the current picture, if any */
static void video_display(VideoState *is)   // 1342
{
  if (is->audio_st && is->show_mode != SHOW_MODE_VIDEO)
    // 12-1. video for audio output
    video_audio_display(is);
  else if (is->video_st)
    // 12-2. video-image output
    video_image_display(is);
}

/* called to display each frame */
static void video_refresh(void *opaque, double *remaining_time)   // 1556
{
  if (is->video_st) {
    /* 11. display picture */
    if (!display_disable && is->force_refresh && is->show_mode == SHOW_MODE_VIDEO && is->pictq.rindex_shown)
      video_display(is);
  }
}

static int queue_picture(VideoState *is, AVFrame *src_frame, double pts, double duration, int64_t pos, int serial)  // 1714
{
  // 7-1. move decoded frame to frame queue
  av_frame_move_ref(vp->frame, src_frame);
}

static int get_video_frame(VideoState *is, AVFrame *frame)    // 1745
{
  // 6-1. decode a frame
  if ((got_picture = decoder_decode_frame(&is->viddec, frame, NULL)) < 0)
    return -1;
}

static int audio_thread(void *arg)    // 2003
{
  do {
    // 14-1. decode a frame
    if ((got_frame = decoder_decode_frame(&is->auddec, frame, NULL)) < 0)
      goto the_end;

    if (got_frame) {
      // 14-2. queue frame
      frame_queue_push(&is->sampq);
    }

  } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
}

static int decoder_start(Decoder *d, int (*fn)(void *), void *arg)  // 2090
{
  // 5. create a decoder thread
  d->decoder_tid = SDL_CreateThread(fn, "decoder", arg);
}

static int video_thread(void *arg)  // 2101
{
  while (true) {
    // 6. get decoded frame
    ret = get_video_frame(is, frame);
    // 7. queue frame
    ret = queue_picture(is, frame, pts, duration, frame->pkt_pos, is->viddec.pkt_serial);
  }
}

/**
 *  return re-sampled audio data
 */
static int audio_decode_frame(VideoState *is)   // 2313
{

}

/* prepare a new audio buffer */
static void sdl_audio_callback(void *opaque, Uint8 *stream, int len)    // 2426
{
  while (len > 0) {
    if (is->audio_buf_index >= is->audio_buf_size) {
      // 13-1-1. re-sampling audio
      audio_size = audio_decode_frame(is);
    }
  }
}

static int audio_open(void *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params) // 2469
{
  // 13-1. SDL audio callback
  wanted_spec.callback = sdl_audio_callback;

  // 13-2. open audio device
  while (!(audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE))) {

  }
}

/* open a given stream. return 0 if OK */
static int stream_component_open(VideoState *is, int stream_index)  // 2543
{
  int ret = 0;

  switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
      /* prepare audio output */
      // 13. audio open
      if ((ret = audio_open(is, channel_layout, nb_channels, sample_rate, &is->audio_tgt)) < 0)
        goto fail;
      // 14. start decoder (thread fn: audio_thread)
      if ((ret = decoder_start(&is->auddec, audio_thread, is)) < 0)
        goto out;
      SDL_PauseAudioDevice(audio_dev, 0);
      break;
    case AVMEDIA_TYPE_VIDEO:
      // 4. start decoder (thread fn: video_thread)
      if ((ret = decoder_start(&is->viddec, video_thread, is)) < 0)
        goto out;
      break;
    default:
      break;
  }
}

/* this thread gets the stream from the disk or the network */
static int read_thread(void *arg)     // 2725
{
  // 3. open stream
  stream_component_open(is, st_index[AVMEDIA_TYPE_VIDEO]);

}

static VideoState *stream_open(const char *filename, AVInputFormat *iformat)  // 3047
{
  VideoState *is;

  is = av_mallocz(sizeof(VideoState));
  if (!is)
    return NULL;
  is->filename = av_strdup(filename);
  if (!is->filename)
    goto fail;

  // TODO

  // 2. create a thread
  is->read_tid = SDL_CreateThread(read_thread, "read_thread", is);
  if (!is->read_tid) {
    av_log(NULL, AV_LOG_FATAL, "SDL_CreateThread(): %s\n", SDL_GetError());
fail:
    stream_close(is);
    return NULL;
  }
  return is;
}

static void refresh_loop_wait_event(VideoState *is, SDL_Event *event)   // 3199
{
  while (!SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT)) {
    if (is->show_mode != SHOW_MODE_NONE && (!is->paused || is->force_refresh))
      // 10. video refresh
      video_refresh(is, &remaining_time);
  }
}

/* handle an event sent by the GUI */
static void event_loop(VideoState *cur_stream)  // 3244
{
  SDL_Event event;

  while (true) {
    // 9. video refresh
    refresh_loop_wait_event(cur_stream, &event);
  }
}

int main(int argc, char *argv[])  // 3645
{
  VideoState *is;

  input_filename = "little.mkv";

  // 1. open stream
  is = stream_open(input_filename, file_iformat);

  // 8. event loop
  event_loop(is);

  return 0;
}
