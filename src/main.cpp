extern "C" {
  #include <libavdevice/avdevice.h>
  #include <libavutil/imgutils.h>
  #include <libswscale/swscale.h>
}

#include <cstdio>

int main(int argc, char *argv[]) {

  if (argc < 2) {
    av_log(nullptr, AV_LOG_INFO, "please provide a movie file\n");
    exit(-1);
  }

  AVFormatContext *pFormatCtx = nullptr;

  // register all formats and codecs
  avdevice_register_all();

  // open a video file
  if (avformat_open_input(&pFormatCtx, argv[1], nullptr, nullptr) != 0) {
    exit(-1);
  }

  // retrieve stream information
  if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
    exit(-1);
  }

  // dump information about file onto standard error
  av_dump_format(pFormatCtx, 0, argv[1], 0);

  int videoStream = -1;

  // find the first video stream
  for (int i = 0; i < pFormatCtx->nb_streams; i++) {
    if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      videoStream = i;
      break;
    }
  }

  if (videoStream == -1) {
    // didn't find a video stream
    exit(-1);
  }

  AVCodec *pCodec = nullptr;
  // find the decoder for the video stream
  pCodec = avcodec_find_decoder(pFormatCtx->streams[videoStream]->codecpar->codec_id);
  if (pCodec == nullptr) {
    fprintf(stderr, "Unsupported codec\n");
    exit(-1);  // codec not found
  }

  AVCodecContext *pCodecCtx = nullptr;

  // get a pointer to the codec context for the video stream
  pCodecCtx = avcodec_alloc_context3(pCodec);

  /* For some codecs, such as msmpeg4 and mpeg4, width and height
   * MUST be initialized there because this information is not
   * available in the bitstream.
   * */
  pCodecCtx->width = pFormatCtx->streams[videoStream]->codecpar->width;
  pCodecCtx->height = pFormatCtx->streams[videoStream]->codecpar->height;

  if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0) {
    exit(-1);
  }

  AVFrame *pFrame = nullptr;
  // allocate video frame
  pFrame = av_frame_alloc();

  AVFrame *pFrameRGB = nullptr;
  // allocate an AVFrame structure
  pFrameRGB = av_frame_alloc();
  if (pFrameRGB == nullptr) {
    exit(-1);
  }

  int numBytes;
  // determine required buffer size and allocate buffer
  numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 32); // 32 bytes

  uint8_t *buffer = nullptr;
  buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));

  // assign appropriate parts of buffer to image planes in pFrameRGB
  // Note that pFrameRGB is an AVFrame, but AVFrame is a superset of AVPicture
//  uint8_t *dst_data[4];
//  int dst_line_size[4];
//  av_image_fill_arrays(dst_data, dst_line_size, buffer, AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 32);

//  struct SwsContext *sws_ctx = nullptr;
//  // initialize SWS context for software scaling
//  sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);

  // free the RGB image
  av_free(buffer);

  // free the video frames
  av_frame_free(&pFrame);   // RGB
  av_frame_free(&pFrameRGB);  // YUV

  // close the codecs
  avcodec_close(pCodecCtx);

  // close the video file
  avformat_close_input(&pFormatCtx);

  return 0;
}