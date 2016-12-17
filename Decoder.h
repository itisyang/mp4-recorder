//
// Created by itisyang.
//

#ifndef FFMPEGDEMO_PPSDECODER_H
#define FFMPEGDEMO_PPSDECODER_H

#include <pthread.h>
#include <jni.h>
#include "ppsdk.h"
#include "FFmpegPlayer.h"
#include "Mp4Recorder.h"
#include "Util.h"
#include "AndroidNativeOpenGl2.h"

//缓存的帧数
#define MAX_FRAME_COUNT 64
//视频缓存大小
#define MAX_FRAME_SIZE 256000
//音频缓存大小
#define MAX_AUDIO_SIZE 400
//缓存的音频帧数
#define MAX_AUDIO_COUNT 400

#define MAX_AUDIO_DROP_FRAMES 50
#define ALL_BUFFER_FRAME_SIZE  MAX_FRAME_COUNT*MAX_FRAME_SIZE
#define ALL_BUFFER_AUDIO_SIZE MAX_AUDIO_COUNT*MAX_AUDIO_SIZE

class PPSDecoder {
public:
    JavaVM *g_jvm;
    Pthread_timewait_t timewait_Video;
    Pthread_timewait_t timewait_Audio;
    Pthread_timewait_t timewait_global;
    //Video
    char frameBuffer[ALL_BUFFER_FRAME_SIZE];          //分配视频缓存空间
    int lFrameSize[MAX_FRAME_COUNT];     //视频帧长度数组
    char *pFramePoints[MAX_FRAME_COUNT]; //视频帧指针
    int lCurFrameIndex;                     //当前缓存视频帧位置
    int lCurDecodeFrameIndex;               //当前解码视频帧位置
    char *pCurFrame;
    //Audio
    char frameAudioBuffer[ALL_BUFFER_AUDIO_SIZE];          //分配音频缓存空间
    int lFrameAudioSize[MAX_AUDIO_COUNT];     //音频帧长度数组
    char *pFrameAudioPoints[MAX_AUDIO_COUNT]; //音频帧指针
    int lCurFrameAudioIndex;                     //当前缓存音频帧位置
    int lCurDecodeFrameAudioIndex;               //当前解码音频帧位置
    char *pCurFrameAudio;

    bool isQuit;
    int lHandle;//Login后的句柄
    pthread_t decoderThreadId;
    pthread_t decoderAudioThreadId;
    long decodeFrameCount;
    long receiveByteCount;
    int videoWidth;
    int videoHeight;
    time_t fpsStartTime;
    time_t btsStartTime;
    int decoderVideoDirect;//解码方向，标识解码和缓存是否在同一个周期内
    int decoderAudioDirect;//解码方向，标识解码和缓存是否在同一个周期内

    int videoSize;
    int currentTime;
    int frameRatio;         //视频帧率
    int mvideoseek;
    int maudioseek;
    //YUV out buffer
    unsigned char * yBuffer;
    unsigned char * uBuffer;
    unsigned char * vBuffer;

    //Audio Player
    jobject audioPlayer;
    unsigned char * audioBuffer;
    bool  isMute;//静音开关
    JNIEnv *envOutAudio;
    jclass clsAudioPlayer;
    jmethodID midAudioPlayerUpdateMethod;


    //SPS PPS
    unsigned char sps_pps[128];

    FFmpegPlayer* fFmpegPlayer;
    Mp4Recorder * mp4Recoder;
    bool isVoiceOpen;
    bool snapShotEnable;    //是否抓取下一帧图像
    jobject snapCallbackInstance;   //抓图输出临时回调
    jobject videoStopCallbackInstance;
    jobject videoSeekCallbackInstance;
    jobject glsurface;
    AndroidNativeOpenGl2Channel* mAndroidOpengl2;
    //add by chenhangfeng
    long videoTimeStamp[MAX_FRAME_COUNT];
    long audioTimeStamp[MAX_AUDIO_COUNT];
    unsigned int videoFormat[MAX_FRAME_COUNT];
    long oldVideoTimeStamp;
    long oldAudioTimeStamp;

    PPSDecoder();
    ~PPSDecoder();
    /**
     * 缓存视频数据帧
     */
    void bufferVideoFrame(char * buffer,int len,int time,unsigned int videotimestamp,int frame_type);
    /**
     * 缓存音频数据帧
     */
    void bufferAudioFrame(char * buffer,int len,unsigned int audiotimestamp);
    /**
     * 绘制出一帧
     */
    void renderFrameYUV();

    /**
     * 输出一个音频帧（录像或播放）
     */
    void outAudioFrame(char *buffer,int len);
    /**
     * 释放资源
     */
    void free();
    /**
     * 流回调
     */
    static void CALLBACK cfg(void *context,int type, PPSDEV_MEDIA_HEADER_PTR header, char *buffer, int len);
    static void CALLBACK voiceCfg(void *context,int type, PPSDEV_MEDIA_HEADER_PTR header, char *buffer, int len);
    /**
     * 视频解码线程运行
     */
    static void * decodeVideoThreadRun(void* context);
    /**
     * 音频解码线程运行
     */
    static void * decodeAudioThreadRun(void* context);

    /**
     * 设备绘图内存区
     */
    void setRenderBuffer(JNIEnv *env,jobject yBuffer,jobject uBuffer,jobject vBuffer);
    /**
     * 设置音频输出内存区
     */
    void setAudioBuffer(JavaVM *jvm,JNIEnv *env,jobject audioPlay,jobject audioBuffer);
    /**
     * 获取FPS
     */
    int getFPS();

    /**
     * 获取BTS
     */
    long getBits();
    /**
     * 重置FPS计数器
     */
    void resetFPS();
    /**
     * 开始录像
     */
    int startRecord(const char *fileName);
    /**
     * 停止录像
     */
    int stopRecord();
    /**
     * 设置准备抓拍
     */
    void setSnapShotEnable(jobject instance);
    /**
     * 输出抓拍结果
     */
    void outSnapShot(int *data,int len);
};


#endif //FFMPEGDEMO_PPSDECODER_H
