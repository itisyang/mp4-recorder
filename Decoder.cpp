//
// 视频解码器
// Created by itisyang.
//

#include <string.h>
#include "PPSDecoder.h"
#include "libfdk-aacenc.h"
#include "ppsdk.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "yuv2bmp.h"

static const void pthread_timewait_init(Pthread_timewait_t *t) {
    pthread_mutexattr_init(&t->mutex_attr);
    pthread_condattr_init(&t->cond_attr);
    pthread_mutex_init(&t->mutex, &t->mutex_attr);
    pthread_cond_init(&t->cond, &t->cond_attr);
    t->isInit = true;
}

static const void pthread_timewait_destroy(Pthread_timewait_t *t) {
    pthread_mutexattr_destroy(&t->mutex_attr);
    pthread_condattr_destroy(&t->cond_attr);
    pthread_mutex_destroy(&t->mutex);
    pthread_cond_destroy(&t->cond);
}

static const void pthread_timewait_sleep_interval(Pthread_timewait_t *t, u_long nHm) {
    gettimeofday(&(t->now), NULL);
    t->now.tv_usec += 1000*nHm;
    if (t->now.tv_usec > 1000000) {
        t->now.tv_sec += t->now.tv_usec / 1000000;
        t->now.tv_usec %= 1000000;
    }

    t->outtime.tv_sec = t->now.tv_sec;
    t->outtime.tv_nsec = t->now.tv_usec * 1000;
    pthread_cond_timedwait(&t->cond, &t->mutex, &t->outtime);
}

PPSDecoder::PPSDecoder() {
    lCurFrameIndex = -1;
    lCurDecodeFrameIndex =0;
    lCurFrameAudioIndex = -1;
    lCurDecodeFrameAudioIndex =0;
    pCurFrame= NULL;
    pCurFrameAudio= NULL;
    isQuit=0;
    decoderVideoDirect=0;
    decoderAudioDirect=0;
    decoderAudioThreadId=0;
    decoderThreadId=0;
	isVoiceOpen=false;
    isMute=true;
    oldVideoTimeStamp = 0;
    oldAudioTimeStamp = 0;
    glsurface = NULL;
    mAndroidOpengl2 = NULL;
    pthread_timewait_init(&timewait_Audio);
    pthread_timewait_init(&timewait_Video);
    pthread_timewait_init(&timewait_global);
    memset(audioTimeStamp,0,MAX_AUDIO_COUNT);
    memset(videoTimeStamp,0,MAX_FRAME_COUNT);
    memset(videoFormat,0,MAX_FRAME_COUNT);
    LOGE("解码器初始化");
}

PPSDecoder::~PPSDecoder() {
    if (mAndroidOpengl2!=NULL)
    {
        LOGE("delete mAndroidOpengl2");
        mAndroidOpengl2->destory();
        //delete mAndroidOpengl2;
        glsurface = NULL;
    }
    pCurFrame= NULL;
    pCurFrameAudio=NULL;
    memset(audioTimeStamp,0,MAX_AUDIO_COUNT);
    memset(videoTimeStamp,0,MAX_FRAME_COUNT);
    memset(videoFormat,0,MAX_FRAME_COUNT);
    pthread_timewait_destroy(&timewait_Audio);
    pthread_timewait_destroy(&timewait_Video);
    pthread_timewait_destroy(&timewait_global);
    LOGE("解码器释放");
}

void PPSDecoder::resetFPS() {
    receiveByteCount=0;
    fpsStartTime=time(NULL);
}

void PPSDecoder::bufferVideoFrame(char *buffer, int len,int time, unsigned int videotimestamp,int frame_type) {
    int nextFrameIndex=lCurFrameIndex+1;
    if(nextFrameIndex>=MAX_FRAME_COUNT){
        nextFrameIndex=0;
        decoderVideoDirect++;
    }
    pCurFrame=&frameBuffer[nextFrameIndex*MAX_FRAME_SIZE];

    if(decoderVideoDirect%2!=0 && nextFrameIndex>=lCurDecodeFrameIndex){
        LOGE("丢弃一些视频帧，nextFrameIndex:%d,lCurDecodeFrameIndex:%d",nextFrameIndex,lCurDecodeFrameIndex);
        lCurDecodeFrameIndex=0;
        decoderVideoDirect++;
    }

    lCurFrameIndex=nextFrameIndex;
    memcpy(pCurFrame,buffer,len);
    memcpy(pCurFrame+len,&time,4);
    lFrameSize[nextFrameIndex]=len;
    pFramePoints[nextFrameIndex]=pCurFrame;
    videoTimeStamp[nextFrameIndex] = videotimestamp;
    videoFormat[nextFrameIndex] = frame_type;
}

void PPSDecoder::bufferAudioFrame(char *buffer, int len, unsigned int audiotimestamp) {
    int nextFrameIndex=lCurFrameAudioIndex+1;
    if(nextFrameIndex>=MAX_AUDIO_COUNT){
        nextFrameIndex=0;
        decoderAudioDirect++;
    }
    pCurFrameAudio=&frameAudioBuffer[nextFrameIndex*MAX_AUDIO_SIZE];
    //如果下一帧存放的索引要越过当前解码的索引，那么就表示溢出
    if(decoderAudioDirect%2!=0 && nextFrameIndex>=lCurDecodeFrameAudioIndex){
        lCurDecodeFrameAudioIndex=0;
        decoderAudioDirect++;
        LOGE("丢弃一些音频帧");
    }
    lCurFrameAudioIndex=nextFrameIndex;
    int datalen =len>MAX_AUDIO_SIZE?MAX_AUDIO_SIZE:len;
    memcpy(pCurFrameAudio,buffer,datalen);
    lFrameAudioSize[nextFrameIndex]=datalen;
    pFrameAudioPoints[nextFrameIndex]=pCurFrameAudio;
    audioTimeStamp[nextFrameIndex] = audiotimestamp;
}

void* PPSDecoder::decodeVideoThreadRun(void *context) {
    PPSDecoder* pThis=(PPSDecoder*)context;
    //一帧数据解码是否完整
    int frameFinished;
    //当前一帧数据和帧大小
    char * frameData = NULL;
    int frameSize;
    //当前待解码的音视频缓冲区索引
    int targetFrameIndex=0;
    int targetaudioIndex = 0;
    //当前待解码的音视频时间戳
    long videotimestamp = 0;
    long audiotimestamp = 0;
    struct timeval tv = {0};
    gettimeofday(&tv,NULL);
    //上一帧的时间戳，用于录制视频
    pThis->oldVideoTimeStamp =  tv.tv_sec * 1000 + tv.tv_usec / 1000;
    //1s内绘制每一帧所需要的时间
    long misec = 1000/pThis->frameRatio;
    //保存YUV绘制后的时间戳，按照帧率按时绘制
    long nextrenderTime =  pThis->oldVideoTimeStamp;
    LOGE("开启视频解码器:pThis->isQuit=%d",pThis->isQuit);
    //开启解码，直到解码退出
    pThis->mvideoseek = 0;
    while(1){
        if (pThis->isQuit)
        {
            break;
        }
        if (pThis->mvideoseek)
        {
            pThis->mvideoseek = 0;
            pThis->lCurDecodeFrameIndex = 0;
            pThis->lCurFrameIndex =0;
        }
        if(pThis->lCurDecodeFrameIndex==pThis->lCurFrameIndex)
        {
            pthread_timewait_sleep_interval(&pThis->timewait_Video,1000/30);
            continue;
        }
        //重置一帧数据解码完成的标志
        frameFinished=0;
        //获取当前待解码的音视频索引
        targetFrameIndex=pThis->lCurDecodeFrameIndex;
        targetaudioIndex=pThis->lCurFrameAudioIndex;
        //如果当前视频帧号有保存视频数据,那么进行解码
        if(pThis->lFrameSize[targetFrameIndex]>0 && pThis->pFramePoints[targetFrameIndex]!=NULL){
            //视频数据及大小保存到临时变量中
            frameData=pThis->pFramePoints[targetFrameIndex];
            frameSize=pThis->lFrameSize[targetFrameIndex];
            //保存当前的视频时间戳
            videotimestamp = pThis->videoTimeStamp[targetFrameIndex];
            //如果当前的待解码音频帧索引>=0，那么保存音频帧时间戳
            if(targetaudioIndex>=0){
                audiotimestamp = pThis->audioTimeStamp[targetaudioIndex];
            } else{
                audiotimestamp = 0;
            }
            //解码一帧视频数据
            frameFinished=pThis->fFmpegPlayer->decodeVideoFrame(frameData,frameSize);
            pThis->currentTime=*(int*)(frameData+frameSize);
            //如果解码成功，那么可以进行多种操作，比如录像，播放
            if(frameFinished)//成功解码
            {
                if(pThis->videoWidth!=pThis->fFmpegPlayer->outputWidth||pThis->videoHeight!=pThis->fFmpegPlayer->outputHeight)
                {
                    LOGE("videosize change old:%d:%d,%d",pThis->videoWidth,pThis->videoHeight,pThis->videoSize);
                    pThis->videoWidth=pThis->fFmpegPlayer->outputWidth;
                    pThis->videoHeight=pThis->fFmpegPlayer->outputHeight;
                    pThis->videoSize=pThis->videoWidth*pThis->videoHeight;
                    LOGE("videosize change new:%d:%d,%d",pThis->videoWidth,pThis->videoHeight,pThis->videoSize);
                }
                long curtimestamp = pThis->videoTimeStamp[targetFrameIndex];
                long nexttimestamp = pThis->videoTimeStamp[(targetFrameIndex+1)%MAX_FRAME_COUNT];
                misec = (nexttimestamp - curtimestamp)>1000?1000:(nexttimestamp - curtimestamp);
                //获取当前的时间
                gettimeofday(&tv,NULL);
                //如果当前解码的视频时间戳要比音频快500ms，那么将音频帧数据给丢掉
                //如果此刻的时间比预计到此刻的时间要播放的快，那么进行相应的等待时间,预计执行到这行时，应该差不多misec的大小间隔
                if((audiotimestamp-videotimestamp)>500&&videotimestamp!=0&&videotimestamp<audiotimestamp&&(tv.tv_sec * 1000 + tv.tv_usec / 1000)<(nextrenderTime+misec))
                {
                    usleep((nextrenderTime+misec-(tv.tv_sec * 1000 + tv.tv_usec / 1000))*1000/2);
                }
                else if((tv.tv_sec * 1000 + tv.tv_usec / 1000)<(nextrenderTime+misec))
                {
                    usleep((nextrenderTime+misec-(tv.tv_sec * 1000 + tv.tv_usec / 1000))*1000);
                }
                //获取当前的时间，保存到nextrenderTime中，供下一次校验
                gettimeofday(&tv,NULL);
                nextrenderTime = tv.tv_sec * 1000 + tv.tv_usec/ 1000;
                //将YUV数据保存到共享缓存中
                pThis->renderFrameYUV();
                //如果开启了录像
                if(pThis->mp4Recoder!=NULL){
                    gettimeofday(&tv,NULL);
                    pThis->mp4Recoder->writeVideoData(frameData,frameSize,(tv.tv_sec * 1000 + tv.tv_usec / 1000-pThis->oldVideoTimeStamp));
                }
                //保存现在播放的时间戳
                pThis->oldVideoTimeStamp = tv.tv_sec * 1000 + tv.tv_usec / 1000;
                //判断是否开启了截图操作
                if(pThis->snapShotEnable && pThis->snapCallbackInstance !=NULL){
                    //抓拍
                    int len=0;
                    int *data=pThis->fFmpegPlayer->getFrameBitmapData(&len);
                    if(len>0){
                        pThis->outSnapShot(data,len);
                    }
                }
                //当前的视频解码数加1
                pThis->decodeFrameCount=pThis->decodeFrameCount+1;
                //如果当前解码数大于1000000，那么重置下
                if(pThis->decodeFrameCount>=1000000){
                    pThis->resetFPS();
                }
            }
            else{
                LOGE("数据异常，解码失败");
            }
            //解码过后当前帧的大小清空，数据清空,待解码帧加1
            pThis->lFrameSize[targetFrameIndex]=0;
            pThis->pFramePoints[targetFrameIndex]=NULL;
            pThis->lCurDecodeFrameIndex=targetFrameIndex+1;
            //如果待解码帧大于最大帧，那么清零
            if(pThis->lCurDecodeFrameIndex>=MAX_FRAME_COUNT){
                pThis->lCurDecodeFrameIndex=0;
                pThis->decoderVideoDirect++;
            }

            //如果当前的帧号，跟接收的帧hao相同，那么等待30ms，如果需要退出，那么这里面就直接退出了
            while(1){
                if(pThis->lCurDecodeFrameIndex==pThis->lCurFrameIndex && !pThis->isQuit)
                {
                    pthread_timewait_sleep_interval(&pThis->timewait_Video,1000/30);
                } else{
                    break;
                }
            }
        }
    }
    //释放录像类资源
    if(pThis->mp4Recoder!=NULL){
        pThis->mp4Recoder->stopRecord();
        pThis->mp4Recoder=NULL;
    }
    //释放解码器
    if(pThis->fFmpegPlayer!=NULL){
        pThis->fFmpegPlayer->isVideoQuit=true;
        if(pThis->fFmpegPlayer->isAudioQuit){
            pThis->fFmpegPlayer->free();
            delete pThis->fFmpegPlayer;
            pThis->fFmpegPlayer=NULL;
        }
    }
    //解码器线程号清零
    pThis->decoderThreadId=0;
    LOGE("解码线程已退出");
    pthread_exit(0);
}

void PPSDecoder::outSnapShot(int *data, int len) {
    if(this->snapCallbackInstance ==NULL && this->g_jvm==NULL){
        this->snapShotEnable=false;
        return;
    }
    this->snapShotEnable=false;
    JNIEnv *env;
    if (this->g_jvm->AttachCurrentThread(&env, NULL) < 0) {
        LOGE( "%s: AttachCurrentThread() failed" , __FUNCTION__);
    }
    jclass cls=env->GetObjectClass(this->snapCallbackInstance);
    jmethodID mid = env->GetMethodID(cls, "snapShotCallback" , "([I)V" );
    if (mid == NULL) {
        LOGE( "GetMethodID() Error....." );
    }
    jintArray bitmapData=env->NewIntArray(len);
    env->SetIntArrayRegion(bitmapData,0,len,(jint*)data);
    env->CallVoidMethod(this->snapCallbackInstance,mid,bitmapData);
    this->g_jvm->DetachCurrentThread();
    this->snapCallbackInstance =NULL;
}

void* PPSDecoder::decodeAudioThreadRun(void *context) {
    PPSDecoder* pThis=(PPSDecoder*)context;
    //音频帧大小及帧数据
    char * frameData;
    int frameSize;
    //目标音频帧索引
    int targetFrameIndex=0;
    //音视频时间戳
    long audiotimestamp = 0;
    long videotimestamp = 0;
    //待解码的视频索引
    unsigned int targetvideoIndex = 0;
    struct timeval tv;
    gettimeofday(&tv,NULL);
    //获取当前的时间
    pThis->oldAudioTimeStamp =  tv.tv_sec * 1000 + tv.tv_usec / 1000;

    //获取当前音频的时间
    long nextplayaudiotimes =  tv.tv_sec * 1000 + tv.tv_usec / 1000;
    //默认音频的帧率是25帧，那么播放的时间间隔是40ms
    long misec = 1000/25;
    long nexttimestamp = 0;
    long curtimestamp = 0;
    uint8_t * audiodata = NULL;
    //解码退出标志
    while(1){
        if (pThis->isQuit)
        {
            break;
        }
        if (pThis->maudioseek)
        {
            pThis->maudioseek = 0;
            pThis->lCurDecodeFrameAudioIndex = 0;
            pThis->lCurFrameAudioIndex =0;
        }
        if(pThis->lCurFrameAudioIndex==pThis->lCurDecodeFrameAudioIndex)
        {
            pthread_timewait_sleep_interval(&pThis->timewait_Audio,1000/60);
            continue;
        }
        //获取当前待解码的音频索引
        targetFrameIndex=pThis->lCurDecodeFrameAudioIndex;
        //如果当前的音频帧有数据
        if(pThis->lFrameAudioSize[targetFrameIndex]>0 && pThis->pFrameAudioPoints[targetFrameIndex]!=NULL){
            //获取音频帧数据和大小
            frameData=pThis->pFrameAudioPoints[targetFrameIndex];
            frameSize=pThis->lFrameAudioSize[targetFrameIndex];
            //获取当前的待解码视频索引
            targetvideoIndex=pThis->lCurDecodeFrameIndex;
            //获取音视频的解码时间戳
            videotimestamp = pThis->videoTimeStamp[targetvideoIndex];
            audiotimestamp = pThis->audioTimeStamp[targetFrameIndex];
            //如果当前解码的视频时间戳要比音频快500ms，那么将音频帧数据给丢掉
            if((int)(videotimestamp-audiotimestamp)>500&&videotimestamp!=0&&audiotimestamp<videotimestamp){

                if((pThis->lCurFrameAudioIndex>=pThis->lCurDecodeFrameAudioIndex?pThis->lCurFrameAudioIndex-pThis->lCurDecodeFrameAudioIndex: \
                  MAX_AUDIO_COUNT-(pThis->lCurDecodeFrameAudioIndex-pThis->lCurFrameAudioIndex))>MAX_AUDIO_DROP_FRAMES)
                {
                    LOGI("now lost audiotimestamp:%ld\n",audiotimestamp);
                    //清空当前的音频帧数据
                    pThis->lFrameAudioSize[targetFrameIndex]=0;
                    pThis->pFrameAudioPoints[targetFrameIndex]=NULL;
                    pThis->lCurDecodeFrameAudioIndex=targetFrameIndex+1;
                    //如果音频帧索引大于最大帧数，那么置0
                    if(pThis->lCurDecodeFrameAudioIndex>=MAX_AUDIO_COUNT){
                        pThis->lCurDecodeFrameAudioIndex=0;
                        pThis->decoderAudioDirect++;
                    }
                    usleep(10000);
                    continue;
                }
            }

            if(1){
                //解码当前的音频数据
                int len = pThis->fFmpegPlayer->decodeAudioFrame(frameData, frameSize);
                //如果当前的解码后长度大于0，并且jvm还存在，那么解码成功
                if(len>0 && pThis->g_jvm!=NULL)
                {
                    //获取解码后的音频数据
                    audiodata = pThis->fFmpegPlayer->pAudioFrame->data[0];
                    //如果当前处于录像时间,那么录制音频数据
                    if(pThis->mp4Recoder!=NULL){
                        //获取当前的时间
                        gettimeofday(&tv,NULL);
                        //录制的时候，将时间差记录
                        pThis->mp4Recoder->writeAudioData(audiodata,len,tv.tv_sec * 1000 + tv.tv_usec / 1000-pThis->oldAudioTimeStamp);
                    }
                    //保存当前的时间
                    pThis->oldAudioTimeStamp = tv.tv_sec * 1000 + tv.tv_usec / 1000;
                    //如果env是空的
                    if(pThis->envOutAudio==NULL){
                        JNIEnv *env;
                        if (pThis->g_jvm->AttachCurrentThread(&env, NULL) < 0) {
                            LOGE( "%s: AttachCurrentThread() failed" , __FUNCTION__);
                            continue;
                        }
                        pThis->envOutAudio=env;
                    }
                    //如果当前的音频播放类是空的
                    if(pThis->clsAudioPlayer==NULL){
                        jclass cls=pThis->envOutAudio->GetObjectClass(pThis->audioPlayer);
                        if(cls==NULL){
                            LOGE( "%s: Find audioplayer Class failed" , __FUNCTION__);
                            continue;
                        }
                        pThis->clsAudioPlayer=cls;
                    }
                    //如果音频的更新方法是空的
                    if(pThis->midAudioPlayerUpdateMethod==NULL){
                        jmethodID mid = pThis->envOutAudio->GetMethodID(pThis->clsAudioPlayer, "update" , "(I)V" );
                        if (mid == NULL) {
                            LOGE( "GetMethodID() Error....." );
                            continue;
                        }
                        pThis->midAudioPlayerUpdateMethod=mid;
                    }
                    //如果当前处于静音，语音对讲，或者退出
                    if (pThis->isMute||pThis->isVoiceOpen||pThis->isQuit)
                    {
                        curtimestamp = pThis->audioTimeStamp[targetFrameIndex];
                        nexttimestamp = pThis->audioTimeStamp[(targetFrameIndex+1)%MAX_AUDIO_COUNT];
                        misec = (nexttimestamp - curtimestamp)>1000/25?1000/25:(nexttimestamp - curtimestamp);
                        gettimeofday(&tv,NULL);
                        if ((tv.tv_sec * 1000 + tv.tv_usec / 1000)<(nextplayaudiotimes+misec)){
                            usleep((nextplayaudiotimes+misec-(tv.tv_sec * 1000 + tv.tv_usec / 1000))*1000);
                        }
                        gettimeofday(&tv,NULL);
                        nextplayaudiotimes = tv.tv_sec * 1000 + tv.tv_usec/ 1000;
                    } else{
                        curtimestamp = pThis->audioTimeStamp[targetFrameIndex];
                        nexttimestamp = pThis->audioTimeStamp[(targetFrameIndex+1)%MAX_AUDIO_COUNT];
                        misec = (nexttimestamp - curtimestamp)>1000/25?1000/25:(nexttimestamp - curtimestamp);
                        //记录当前的时间,确保音频播放在1000/25的时间间隔
                        gettimeofday(&tv,NULL);
                        if ((tv.tv_sec * 1000 + tv.tv_usec / 1000)<(nextplayaudiotimes+misec)){
                            usleep((nextplayaudiotimes+misec-(tv.tv_sec * 1000 + tv.tv_usec / 1000))*1000);
                        }
                        gettimeofday(&tv,NULL);
                        //记录当前的时间
                        nextplayaudiotimes = tv.tv_sec * 1000 + tv.tv_usec/ 1000;
                        //将声音放到音频队列中
                        pThis->outAudioFrame((char *) audiodata,len);
                    }
                    //如果当前的解码帧数和总的帧数相同，那么表示当前没有新的音频帧
                    while(1){
                        if(((targetFrameIndex+1)%MAX_AUDIO_COUNT)==pThis->lCurFrameAudioIndex && !pThis->isQuit){
                            pthread_timewait_sleep_interval(&pThis->timewait_Audio,1000/60);
                        } else{
                            break;
                        }

                    }
                }
                else{
                    LOGE("数据异常，解码失败");
                }
            }
            else{
                pthread_timewait_sleep_interval(&pThis->timewait_Audio,20);
            }
            //清空当前的帧
            pThis->lFrameAudioSize[targetFrameIndex]=0;
            pThis->pFrameAudioPoints[targetFrameIndex]=NULL;
            pThis->lCurDecodeFrameAudioIndex=targetFrameIndex+1;
            if(pThis->lCurDecodeFrameAudioIndex>=MAX_AUDIO_COUNT){
                pThis->lCurDecodeFrameAudioIndex=0;
                pThis->decoderAudioDirect++;
            }
        }
        else{
            pthread_timewait_sleep_interval(&pThis->timewait_Audio,20);
        }
    }
    //释放当前的线程
    if(pThis->envOutAudio!=NULL){
        pThis->g_jvm->DetachCurrentThread();
        pThis->envOutAudio=NULL;
    }
    //释放解码资源
    if(pThis->fFmpegPlayer!=NULL){
        pThis->fFmpegPlayer->isAudioQuit=true;
        if(pThis->fFmpegPlayer->isVideoQuit){
            pThis->fFmpegPlayer->free();
            delete pThis->fFmpegPlayer;
            pThis->fFmpegPlayer=NULL;
        }
    }
    //释放音频播放器
    if(pThis->audioPlayer!=NULL){
        JNIEnv *env;
        if (pThis->g_jvm->AttachCurrentThread(&env, NULL) < 0) {
            LOGE( "%s: AttachCurrentThread() failed" , __FUNCTION__);
        }
        jclass cls=env->GetObjectClass(pThis->audioPlayer);
        jmethodID mid = env->GetMethodID(cls, "stop" , "()V" );
        if (mid == NULL) {
            LOGE( "GetMethodID() Error....." );
        }
        env->CallVoidMethod(pThis->audioPlayer,mid);
        pThis->g_jvm->DetachCurrentThread();
    }
    //清空线程索引
    pThis->decoderAudioThreadId=0;
    LOGE("音频解码线程已退出");
    pthread_exit(0);
}

//预览码流回调
void CALLBACK PPSDecoder::cfg(void *context,int type, PPSDEV_MEDIA_HEADER_PTR header, char *buffer, int len)
{
    PPSDecoder* pThis=(PPSDecoder*)context;
    if (len>0)
    {
        pThis->receiveByteCount=pThis->receiveByteCount+len;
    }

    if(pThis==NULL){
        return;
    }
    if(pThis->isQuit){
        return;
    }

    if(type == SDK_STREAM_DATA_VIDEO)
    {
        if(len>24 && buffer[0]==0x00 && buffer[1]==0x00 && buffer[2]==0x00 && buffer[3]==0x01 && buffer[4]==0x67){
            //获取SPS和PPS
            memcpy(pThis->sps_pps,buffer,len>128?128:len);
        }
        if(pThis->fFmpegPlayer==NULL && !pThis->isQuit) {
            //初始化FFMPEG
            pThis->videoWidth=header->video.width*8;
            pThis->videoHeight=header->video.height*8;
            LOGE("获取视频宽高：%d,%d",pThis->videoWidth,pThis->videoHeight);
            pThis->fFmpegPlayer=new FFmpegPlayer();
            if(header->video.frame_rate>0&&header->video.frame_rate<60){
                pThis->frameRatio=header->video.frame_rate;
                LOGE("取出帧率2为:%d",header->video.frame_rate);
            }else{
                pThis->frameRatio = 12;
                LOGE("码流头取不到帧率，默认12帧");
            }
            pThis->fFmpegPlayer->initFFMpeg(pThis->videoWidth,pThis->videoHeight,header->video.frame_rate);
            pThis->videoSize=pThis->videoWidth*pThis->videoHeight;
            //启动解码线程
            pthread_create( &pThis->decoderThreadId, NULL, pThis->decodeVideoThreadRun, pThis);
            pThis->resetFPS();
            pThis->receiveByteCount=0;
            pThis->btsStartTime=time(NULL);
            pThis->oldVideoTimeStamp = header->timestamp;
            pThis->lCurFrameIndex = -1;
            pThis->lCurDecodeFrameIndex =0;
            pThis->lCurFrameAudioIndex = -1;
            pThis->lCurDecodeFrameAudioIndex =0;
        }
        //缓存视频流
        if(len>512*1024){
            LOGI("RECV VIDEO SIZE:%d",len);
        }
        pThis->bufferVideoFrame(buffer,len,header->datetime,header->timestamp,header->frame_type);
    }
    else if(type == SDK_STREAM_DATA_AUDIO)
    {
        //音频流G711
        if(pThis->fFmpegPlayer!=NULL && !pThis->isQuit && pThis->decoderAudioThreadId==0){
            //启动解码线程
            pthread_create( &pThis->decoderAudioThreadId, NULL, pThis->decodeAudioThreadRun, pThis);
        }
        //缓存音频流
        if(pThis->fFmpegPlayer!=NULL && pThis->audioPlayer!=NULL&& !pThis->isQuit) {
            pThis->bufferAudioFrame(buffer,len,header->timestamp);
        }
    }
    else if (type == SDK_STREAM_DATA_SEEK)
    {
        pThis->lCurFrameIndex = -1;
        pThis->lCurDecodeFrameIndex = 0;
        pThis->lCurDecodeFrameAudioIndex = 0;
        pThis->lCurFrameAudioIndex = -1;
        LOGE("Stream seek close");
        if(pThis->videoSeekCallbackInstance!=NULL){
            JNIEnv *env;
            if (pThis->g_jvm->AttachCurrentThread(&env, NULL) < 0) {
                LOGE( "%s: AttachCurrentThread() failed" , __FUNCTION__);
            }
            jclass cls=env->GetObjectClass(pThis->videoSeekCallbackInstance);
            jmethodID mid = env->GetMethodID(cls, "videoSeekCallback" , "()V" );
            if (mid == NULL) {
                LOGE( "GetMethodID() Error....." );
            }
            env->CallVoidMethod(pThis->videoSeekCallbackInstance,mid);
            pThis->g_jvm->DetachCurrentThread();
        }
        pThis->mvideoseek = 1;
        pThis->maudioseek = 1;
    }
    else if(type == SDK_STREAM_DATA_PAUSE)
    {

    }
    else if (type ==SDK_STREAM_DATA_CLOSE)
    {
        LOGE("Stream close start");
        if(pThis->videoStopCallbackInstance!=NULL){
            JNIEnv *env;
            if (pThis->g_jvm->AttachCurrentThread(&env, NULL) < 0) {
                LOGE( "%s: AttachCurrentThread() failed" , __FUNCTION__);
            }
            jclass cls=env->GetObjectClass(pThis->videoStopCallbackInstance);
            jmethodID mid = env->GetMethodID(cls, "videoCloseCallback" , "()V" );
            if (mid == NULL) {
                LOGE( "GetMethodID() Error....." );
            }
            env->CallVoidMethod(pThis->videoStopCallbackInstance,mid);
            pThis->g_jvm->DetachCurrentThread();
        }
         LOGE("Stream close end");
    }

    return;
}

//对讲码流回调
void CALLBACK PPSDecoder::voiceCfg(void *context,int type, PPSDEV_MEDIA_HEADER_PTR header, char *buffer, int len)
{

    PPSDecoder* pThis=(PPSDecoder*)context;
    if(pThis==NULL){
        return;
    }
    if(type == SDK_STREAM_DATA_VIDEO)
    {

    }
    else if(type == SDK_STREAM_DATA_AUDIO)
    {
        //音频流G711

    }
    else if (SDK_STREAM_DATA_CLOSE)
    {
        //预览结束,，释放你在该回调中分配的内存资源
    }

    return;
}


void PPSDecoder::setRenderBuffer(JNIEnv *env, jobject yBuffer, jobject uBuffer, jobject vBuffer) {

    this->yBuffer=(unsigned char*)(*env).GetDirectBufferAddress(yBuffer);
    this->uBuffer=(unsigned char*)(*env).GetDirectBufferAddress(uBuffer);
    this->vBuffer=(unsigned char*)(*env).GetDirectBufferAddress(vBuffer);
}

void PPSDecoder::setAudioBuffer(JavaVM *jvm, JNIEnv *env, jobject audioPlay,jobject audioBuffer) {
    this->g_jvm=jvm;
    this->audioPlayer=audioPlay;
    this->audioBuffer=(unsigned char*)(*env).GetDirectBufferAddress(audioBuffer);
    jclass cls=env->GetObjectClass(this->audioPlayer);
    jmethodID mid = env->GetMethodID(cls, "play" , "()V" );
    if (mid == NULL) {
        LOGE( "GetMethodID() Error....." );
        return;
    }
    LOGE("开启音频播放器");
    env->CallVoidMethod(this->audioPlayer,mid);
    LOGE("开启音频播放器end");
}

void PPSDecoder::renderFrameYUV() {
    if(this->fFmpegPlayer->pFrame->data[0]!=NULL){
        if (isQuit)
        {
            return;
        }
        int y_len = this->videoSize;
        int u_len = this->videoSize>>2;
        int v_len = this->videoSize>>2;
        memcpy(mAndroidOpengl2->_buffer,this->fFmpegPlayer->pFrame->data[0],y_len);
        memcpy(mAndroidOpengl2->_buffer+y_len,this->fFmpegPlayer->pFrame->data[1],u_len);
        memcpy(mAndroidOpengl2->_buffer+y_len+u_len,this->fFmpegPlayer->pFrame->data[2],v_len);
        mAndroidOpengl2->DeliverFrame(videoWidth,videoHeight);
    }
}

void PPSDecoder::outAudioFrame(char *buffer,int len){
    if(!this->isMute && !this->isQuit && this->audioPlayer!=NULL && this->audioBuffer!=NULL){
        memcpy(this->audioBuffer,buffer,len);
        this->envOutAudio->CallVoidMethod(this->audioPlayer,this->midAudioPlayerUpdateMethod,len);
    }
}

int PPSDecoder::getFPS() {
    time_t tt=time(NULL);
    if((tt-fpsStartTime)==0){
        return 0;
    }
    return decodeFrameCount/(tt-fpsStartTime);
}

long PPSDecoder::getBits() {
    time_t tt=time(NULL);
    if((tt-btsStartTime)==0){
        return 0;
    }
    long result= receiveByteCount/(tt-btsStartTime);

    btsStartTime=time(NULL);
    receiveByteCount=0;
    return result;
}

void PPSDecoder::free() {
    isQuit=true;
    LOGE("准备退出！");
    while(this->decoderAudioThreadId!=0 || this->decoderThreadId!=0){
        LOGE("等待解码线程退出");
        pthread_timewait_sleep_interval(&timewait_global,500);
    }

    LOGE("解码器已退出！");
}

int PPSDecoder::startRecord(const char *fileName) {
    Mp4Recorder *recorder=new Mp4Recorder();
    int ret=recorder->startRecord(fileName,this->videoWidth,this->videoHeight,this->frameRatio,sps_pps);
    if(ret>=0){
        this->mp4Recoder=recorder;
    }
    return ret;
}

int PPSDecoder::stopRecord() {
    if(this->mp4Recoder==NULL){
        return 0;
    }
    Mp4Recorder * recorder=this->mp4Recoder;
    this->mp4Recoder=NULL;
    bool isAvailable=recorder->isAvailable;
    recorder->stopRecord();
    return isAvailable;
}

void PPSDecoder::setSnapShotEnable(jobject instance) {
    this->snapCallbackInstance =instance;
    this->snapShotEnable=true;
}
