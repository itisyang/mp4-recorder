//
// MP4录像类，用于H264原码流进行mp4输出
// Created by itisyang.
//

#ifndef PPSTRONGVIDEO_MP4RECORDER_H
#define PPSTRONGVIDEO_MP4RECORDER_H
extern "C"
{
#include "mp4v2/mp4v2.h"
#include "mp4v2/general.h"
#include "mp4v2/file.h"
#include "mp4v2/file_prop.h"
#include "mp4v2/track.h"
#include "libfdk-aacenc.h"
}

class Mp4Recorder {
    private:
        MP4FileHandle mp4FileHandle;
        MP4TrackId mp4VideoTrackID;
        MP4TrackId mp4AudioTrackID;
        AACEncodeContext *aacEncCtx;
        /**
         * 初始化FDK-AAC编码器
         */
        int initAACEncoder();
    public:
        bool isAvailable;
        bool FoundNF;
        int m_vWidth,m_vHeight,m_vFrateR,m_vTimeScale;
        double m_vFrameDur;
    /**
         * 开始录像
         */
        int startRecord(const char* fileName, int width,int height,int frame_rate,unsigned char *sps_pps);
        /**
         * 停止录像
         */
        void stopRecord();
        /**
         * 写入视频数据
         */
        void writeVideoData(char * buffer,int size,int duration);
        /**
        * 写入音频数据
        */
        void writeAudioData(uint8_t* buffer,int size,int duration);
};


#endif //PPSTRONGVIDEO_MP4RECORDER_H
