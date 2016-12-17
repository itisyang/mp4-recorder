//
// Created by itisyang.
//
#include <string.h>

extern "C"{
#ifdef __cplusplus
 #define __STDC_CONSTANT_MACROS
 #ifdef _STDINT_H
  #undef _STDINT_H
 #endif
 # include <stdint.h>
#endif
}
extern "C"
{
    #include "libavformat/avformat.h"
    #include "libswscale/swscale.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/avutil.h"
    #include "libavutil/frame.h"
    #include "libavutil/channel_layout.h"
    #include "ppsdk.h"
}
#ifndef FFMPEGDEMO_FFMPEGPLAYER_H
#define FFMPEGDEMO_FFMPEGPLAYER_H

//using namespace std;
class FFmpegPlayer {
    private:
    AVCodecContext *pVideoCodecCtx;
    AVCodecContext *pAudioCodecCtx;
    AVPacket packet;
    AVPicture picture;
    struct SwsContext *img_convert_ctx;
public:
    float outputWidth;
    float outputHeight;

public:
    AVFrame *pFrame;
    AVFrame *pAudioFrame;
    bool isVideoQuit;   //视频是否已退出
    bool isAudioQuit;   //音频是否已退出

    int initFFMpeg(int videoWidth,int videoHeight,int frameRate);
    int decodeVideoFrame(char * buffer,int size);
    int decodeAudioFrame(char * buffer,int size);
    int* getFrameBitmapData(int*len);
	int encodePcmToG711(short* buffer,int len,char *g711buffer);
    char * getAudioBuffer();
    int getAudioBufferSize();
    void free();
};


#endif //FFMPEGDEMO_FFMPEGPLAYER_H
