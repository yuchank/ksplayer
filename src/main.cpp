extern "C" {
  #include <libavdevice/avdevice.h>
}

int main(int argc, char *argv[]) {

  if (argc < 2) {
    av_log(nullptr, AV_LOG_INFO, "please provide a movie file\n");
    return -1;
  }

  AVFormatContext *pFormatCtx = nullptr;

  // register all formats and codecs
  avdevice_register_all();

  // open a video file
  if (avformat_open_input(&pFormatCtx, argv[1], nullptr, nullptr) != 0) {
    return -1;
  }

  // retrieve stream information
  if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
    return -1;
  }

  // dump information about file onto standard error
  av_dump_format(pFormatCtx, 0, argv[1], 0);

  // close the video file
  avformat_close_input(&pFormatCtx);

  return 0;
}