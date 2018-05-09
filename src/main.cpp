extern "C" {
  #include <libavdevice/avdevice.h>
  #include <libavutil/imgutils.h>
  #include <libswscale/swscale.h>
}

int decode(AVCodecContext *avctx, AVFrame *frame, int *got_frame, AVPacket *pkt);
void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame);

int main(int argc, char *argv[]) {

  if (argc < 2) {
    av_log(nullptr, AV_LOG_INFO, "please provide a movie file.");
    exit(-1);
  }

  // register all formats and codecs
  avdevice_register_all();

  // do global initialization of network components.
  avformat_network_init();

  // open video file
  AVFormatContext *pFormatCtx = nullptr;

  if (avformat_open_input(&pFormatCtx, argv[1], nullptr, nullptr) != 0) {
    exit(-1);
  }

  // retrieve stream information
  // populate pFormatCtx->streams
  if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
    exit(-1);
  }

  // dump information about file onto standard error
  av_dump_format(pFormatCtx, 0, argv[1], 0);

  // find the first video stream
  int videoStream = -1;
  for (int i = 0; i < pFormatCtx->nb_streams; i++) {
    if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      videoStream = i;
      break;
    }
  }
  if (videoStream == -1) {
    exit(-1);
  }

  int width = pFormatCtx->streams[videoStream]->codecpar->width;
  int height = pFormatCtx->streams[videoStream]->codecpar->height;

  // get a pointer to the codec context for the video stream
  AVCodec *pVideoCodec = avcodec_find_decoder(pFormatCtx->streams[videoStream]->codecpar->codec_id);
  if (pVideoCodec == nullptr) {
    fprintf(stderr, "Unsupported codec\n");
    exit(-1);
  }
  AVCodecContext *pVCtx = avcodec_alloc_context3(pVideoCodec);

  // open code
  if (avcodec_open2(pVCtx, pVideoCodec, nullptr) < 0) {
    exit(-1);
  }

  // allocate video frame
  AVFrame *pVFrame = av_frame_alloc();
  // allocate RGB frame
  AVFrame *pRGBFrame = av_frame_alloc();
  if (pVFrame == nullptr || pRGBFrame == nullptr) {
    exit(-1);
  }

  // determine required buffer size and allocate buffer
  int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, width, height, 1);
  auto *buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));

  // assign appropriate parts of buffer to image planes in pFrameRGB
  // Note that pFrameRGB is an AVFrame, but AVFrame is a superset of AVPicture
  av_image_fill_arrays(pRGBFrame->data, pRGBFrame->linesize, buffer, AV_PIX_FMT_RGB24, width, height, 1);

  // initialize SWS context for software scaling
  struct SwsContext *sws_ctx = sws_getContext(width, height, AV_PIX_FMT_YUV420P, width, height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);

  AVPacket packet;
  int frameFinished;
  int i = 0;
  while (av_read_frame(pFormatCtx, &packet) >= 0) {
    if (packet.stream_index == videoStream) {
      decode(pVCtx, pVFrame, &frameFinished, &packet);

      if (frameFinished) {
        sws_scale(sws_ctx, (uint8_t const * const *)pVFrame->data, pVFrame->linesize, 0, height, pRGBFrame->data, pRGBFrame->linesize);
        i++;
        if (i > 10000 && i <= 10005) {
          SaveFrame(pRGBFrame, width, height, i);
        }
      }
    }
    av_packet_unref(&packet);
  }

  // free the RGB image
  av_free(buffer);
  av_frame_free(&pRGBFrame);

  // free the YUV frame
  av_frame_free(&pVFrame);

  // close the codec
  avcodec_close(pVCtx);

  // close the video file
  avformat_close_input(&pFormatCtx);
}

int decode(AVCodecContext *avctx, AVFrame *frame, int *got_frame, AVPacket *pkt)
{
  int ret;

  *got_frame = 0;

  if (pkt) {
    ret = avcodec_send_packet(avctx, pkt);
    // In particular, we don't expect AVERROR(EAGAIN), because we read all
    // decoded frames with avcodec_receive_frame() until done.
    if (ret < 0) {
      return ret == AVERROR_EOF ? 0 : ret;
    }
  }

  ret = avcodec_receive_frame(avctx, frame);
  if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
    return ret;
  if (ret >= 0)
    *got_frame = 1;

  return 0;
}

void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame)
{
  FILE *pFile;
  char szFilename[32];
  int  y;

  // Open file
  sprintf(szFilename, "frame%d.ppm", iFrame);
  pFile = fopen(szFilename, "wb");
  if (pFile == nullptr)
    return;

  // Write header
  fprintf(pFile, "P6\n%d %d\n255\n", width, height);

  // Write pixel data
  for (y = 0; y < height; y++)
    fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width*3, pFile);

  // Close file
  fclose(pFile);
}