//
// Created by itisyang.
//

#include "FFmpegPlayer.h"
#include "util.h"

int FFmpegPlayer::initFFMpeg(int videoWidth,int videoHeight,int frameRate) {
    // Register all formats and codecs
    av_register_all();
    avcodec_register_all();


    /* find the video encoder */
    AVCodec *videoCodec = avcodec_find_decoder(AV_CODEC_ID_H264);
    pVideoCodecCtx = avcodec_alloc_context3(videoCodec);

    if (!videoCodec)
    {
        LOGE("codec not found!" );
        return -1;
    }

    //初始化参数，下面的参数应该由具体的业务决定
    pVideoCodecCtx->frame_number = 1; //每包一个视频帧
    pVideoCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    pVideoCodecCtx->bit_rate = 0;
    pVideoCodecCtx->time_base.num = 1;
    pVideoCodecCtx->time_base.den = frameRate;//帧率
    pVideoCodecCtx->pix_fmt=AV_PIX_FMT_YUV420P;
    pVideoCodecCtx->width = videoWidth;//视频宽
    pVideoCodecCtx->height =videoHeight;//视频高
    outputWidth=videoWidth;
    outputHeight=videoHeight;
    if(avcodec_open2(pVideoCodecCtx, videoCodec,NULL) >= 0) {
        pFrame = av_frame_alloc();// Allocate video frame
    }
    else {
        return -1;
    }
    avpicture_free(&picture);
    sws_freeContext(img_convert_ctx);

    // Allocate RGB picture
    avpicture_alloc(&picture, AV_PIX_FMT_BGRA, videoWidth, videoHeight);

    // Setup scaler
    static int sws_flags =  SWS_FAST_BILINEAR;

    img_convert_ctx = sws_getContext(videoWidth,
                                     videoHeight,
                                          pVideoCodecCtx->pix_fmt,
                                          outputWidth,
                                          outputHeight,
                                          AV_PIX_FMT_BGRA,
                                          sws_flags, NULL, NULL, NULL);

    //音频解码初始化
    //音频格式--->G711u
    AVCodec *audioCodec = avcodec_find_decoder(AV_CODEC_ID_PCM_MULAW);
    if (!audioCodec) {
        LOGE("没有找到该音频解码器");
        return -2;
    }
//    AVSampleFormat format=AV_SAMPLE_FMT_S16;
//    audioCodec->sample_fmts= &format;
    pAudioCodecCtx = avcodec_alloc_context3(audioCodec);

    //初始化参数，下面的参数应该由具体的业务决定
    pAudioCodecCtx->frame_number = 1;//每包一个音频帧
    pAudioCodecCtx->codec_type   = AVMEDIA_TYPE_AUDIO;
    pAudioCodecCtx->channels     = 1;
//    pAudioCodecCtx->profile=FF_PROFILE_AAC_LOW;
    pAudioCodecCtx->sample_rate=8000;
    pAudioCodecCtx->sample_fmt=AV_SAMPLE_FMT_S16;
    pAudioCodecCtx->bit_rate=64000;

    if (avcodec_open2(pAudioCodecCtx, audioCodec, NULL) >= 0) {
        pAudioFrame = av_frame_alloc();
    } else {
        LOGE("创建解码器失败");
    }


    isVideoQuit=false;
    isAudioQuit=false;
    LOGE("ffmpeg init");
    return 1;
}

int FFmpegPlayer::decodeVideoFrame(char * buffer,int size) {
    AVPacket packet = {0};
    av_init_packet(&packet);
    packet.data = (uint8_t *)buffer;//这里填入一个指向完整H264数据帧的指针
    packet.size = size;//这个填入H264数据帧的大小

    int frameFinished=0;
    avcodec_decode_video2(pVideoCodecCtx, pFrame, &frameFinished, &packet);

    av_free_packet(&packet);
    return frameFinished;
}
int FFmpegPlayer::decodeAudioFrame(char * buffer,int size) {
    AVPacket packet = {0};
    packet.data = (uint8_t *)buffer;//这里填入一个指向完整G711数据帧的指针
    packet.size = size;//这个填入G711数据帧的大小
    int frameFinished=0;
    avcodec_decode_audio4(pAudioCodecCtx, pAudioFrame, &frameFinished, &packet);
    if(frameFinished<0){
        return frameFinished;
    }
    int data_size = pAudioFrame->linesize[0];
    return data_size;
}

int FFmpegPlayer::getAudioBufferSize() {
    return 0;
}
int* FFmpegPlayer::getFrameBitmapData(int*len){
    if (!pFrame->data[0]) {
        return NULL;
    }
    if(img_convert_ctx)
    {
        sws_scale(img_convert_ctx,pFrame->data, pFrame->linesize, 0,outputHeight,
                  picture.data, picture.linesize);
    }
    *len= picture.linesize[0]*outputHeight/4;
    return (int*)picture.data[0];
}


void FFmpegPlayer::free() {

    // Free scaler
    //if (img_convert_ctx != NULL) { }
       sws_freeContext(img_convert_ctx);
        img_convert_ctx = NULL;
    //}
    // Free RGB picture
   // if(picture!=NULL){
    avpicture_free(&picture);
    //}
    // Free the packet that was allocated by av_read_frame
    av_free_packet(&packet);
    // Free the YUV frame
    av_free(pFrame);

    // Close the codec
    if (pVideoCodecCtx) {
        avcodec_close(pVideoCodecCtx);
        pVideoCodecCtx=NULL;
    }

    if(pAudioCodecCtx){
        avcodec_close(pAudioCodecCtx);
        pAudioCodecCtx=NULL;
    }
    LOGE("ffmpeg free");


}
//#define	SIGN_BIT	(0x80)		/* Sign bit for a A-law byte. */
//#define	QUANT_MASK	(0xf)		/* Quantization field mask. */
//#define	NSEGS		(8)		/* Number of A-law segments. */
//#define	SEG_SHIFT	(4)		/* Left shift for segment number. */
//#define	SEG_MASK	(0x70)		/* Segment field mask. */

static short seg_end[8] = {0xFF, 0x1FF, 0x3FF, 0x7FF,
                           0xFFF, 0x1FFF, 0x3FFF, 0x7FFF};
static int search(int		val, short		*table, int		size)
{
    int		i;

    for (i = 0; i < size; i++) {
        if (val <= *table++)
            return (i);
    }
    return (size);
}
#define	BIAS		(0x84)		/* Bias for linear code. */
unsigned char linear2ulaw(int		pcm_val)	/* 2's complement (16-bit range) */
{
    int		mask;
    int		seg;
    unsigned char	uval;

    /* Get the sign and the magnitude of the value. */
    if (pcm_val < 0) {
        pcm_val = BIAS - pcm_val;
        mask = 0x7F;
    } else {
        pcm_val += BIAS;
        mask = 0xFF;
    }

    /* Convert the scaled magnitude to segment number. */
    seg = search(pcm_val, seg_end, 8);

    /*
     * Combine the sign, segment, quantization bits;
     * and complement the code word.
     */
    if (seg >= 8)		/* out of range, return maximum value. */
        return (0x7F ^ mask);
    else {
        uval = (seg << 4) | ((pcm_val >> (seg + 3)) & 0xF);
        return (uval ^ mask);
    }

}
int FFmpegPlayer::encodePcmToG711(short *buffer, int len,char *g711buffer) {
        unsigned short* src = (unsigned short*)buffer;
        unsigned char* dst =(unsigned char*) g711buffer;//(unsigned char*)malloc(sizeof(unsigned char) * (len/2));
        unsigned short i;
        short data0;
        int  data;

        for (i = 0; i < len; i++) {
            data0 = *(src + i);
            data=data0;
            *(dst + i )=linear2ulaw(data);
        }
        //char* pdata = (char*)(&(dst[0]));

        return 1;
}
