//
// Created by itisyang.
//

#include "Mp4Recorder.h"
#include <arpa/inet.h>
#include "util.h"

int Mp4Recorder::initAACEncoder() {
    if (aac_encode_init(&aacEncCtx, 1, 8000, 320) != 0) {
        LOGE("初始化AAC编码器失败");
        return -1;
    }
    return 0;
}

int Mp4Recorder::startRecord(const char* fileName, int width,int height,int frame_rate,unsigned char *sps_pps) {
    m_vWidth = width;
    m_vHeight = height;
    m_vFrateR = frame_rate;
    m_vTimeScale = 90000;
    m_vFrameDur = 3000;
    mp4FileHandle= (MP4FileHandle)MP4Create(fileName,0);
    if(mp4FileHandle==MP4_INVALID_FILE_HANDLE){
        LOGE("创建MP4文件失败！");
        return -1;
    }
    MP4SetTimeScale(mp4FileHandle,m_vTimeScale);
    mp4VideoTrackID=MP4AddH264VideoTrack(mp4FileHandle,m_vTimeScale,m_vTimeScale/m_vFrateR,width,height
            ,sps_pps[5],sps_pps[6],sps_pps[7],3);
    if(mp4VideoTrackID == MP4_INVALID_TRACK_ID)
    {
        LOGE("创建视频Track失败！");
        MP4Close(mp4FileHandle);
        mp4FileHandle=NULL;
        return -1;
    }
    mp4AudioTrackID=MP4AddAudioTrack(mp4FileHandle,8000,1024, MP4_MPEG4_AUDIO_TYPE);
    if(mp4AudioTrackID == MP4_INVALID_TRACK_ID)
    {
        LOGE("创建音频Track失败！");
        MP4Close(mp4FileHandle);
        mp4FileHandle=NULL;
        return -1;
    }
    //bit->00010,      1011,      0001,      000 = 0x1588
    //     LOW       采样率编号    单声道
    u_int8_t pConfig[] = {0x15,0x88};
    MP4SetTrackESConfiguration(mp4FileHandle, mp4AudioTrackID, pConfig, 2);
    if (initAACEncoder() < 0) {
        LOGE("初始化AAC编码器失败");
        return -1;
    }
    isAvailable= false;
    MP4SetVideoProfileLevel(mp4FileHandle, 0x01);
    LOGE("开始录制文件%s",fileName);
    return 1;
}

void Mp4Recorder::stopRecord(){
    isAvailable = false;
    FoundNF = false;
    MP4Close(mp4FileHandle);
    aac_free_context(&aacEncCtx);
    LOGE("结束录制文件");
}

void Mp4Recorder::writeAudioData(uint8_t*buffer, int size,int duration) {
    //进行录像输出
    if(!isAvailable&&!FoundNF){
        return;
    }
    if (aac_encode_frame(aacEncCtx, buffer) != 0) {
        return;
    }
    MP4WriteSample(
            mp4FileHandle,
            mp4AudioTrackID,
            aac_get_out_buffer(aacEncCtx),
            aac_get_out_size(aacEncCtx),
            MP4_INVALID_DURATION,
            0,
            true);
    m_vFrameDur += 1024;
}

void Mp4Recorder::writeVideoData(char * buffer,int size,int duration) {
    //进行录像输出
    //进行录像输出
    char * pBuffer=buffer;
    int off=0;
    int findMaxPos = size >> 1;         //size / 2 查找到最大的位置
    bool isFoundSPS = FALSE;            //!< 是否找到SPS帧
    bool isFoundPPS = FALSE;            //!< 是否找到PPS帧
    bool isFoundIEX = FALSE;            //!< 是否找到I帧加强帧
    bool isFoundNF  = FALSE;            //!< 是否找到普通帧
    int spsLoc,ppsLoc,iexLoc;           //!< 帧信息开始的位置
    int spsLen,ppsLen,iexLen;           //!< 帧信息的长度
    spsLoc = ppsLoc = iexLoc = spsLen = ppsLen = iexLen = 0;
    for (off = 0; off < findMaxPos; off++) {
        if (pBuffer[off]==0x00 && pBuffer[off + 1]==0x00 && pBuffer[off + 2]==0x00 && pBuffer[off + 3]==0x01 && pBuffer[off + 4]==0x67 && !isFoundSPS) {
            isFoundSPS = TRUE;
            off += 4;
            spsLoc = off;//SPS帧的开始位置
            continue;
        } else if (!isFoundPPS && !isFoundIEX && isFoundSPS) {
            spsLen++;
        }
        if(pBuffer[off+0]==0x00 && pBuffer[off+1]==0x00 && pBuffer[off+2]==0x00 && pBuffer[off+3]==0x01 && pBuffer[off+4]==0x68 && !isFoundPPS) {
            isFoundPPS = TRUE;
            off += 4;
            ppsLoc = off;//PSP的开始位置
            continue;
        } else if (!isFoundIEX && isFoundPPS) {
            ppsLen++;
        }
        if(pBuffer[off+0]==0x00 && pBuffer[off+1]==0x00 && pBuffer[off+2]==0x00 && pBuffer[off+3]==0x01 && pBuffer[off+4]==0x06 && !isFoundIEX) {
            isFoundIEX = TRUE;
            off += 4;
            iexLoc = off;//I帧加强的开始位置
            continue;
        } else if (isFoundIEX) {
            iexLen++;
        }
        if(pBuffer[off+0]==0x00 && pBuffer[off+1]==0x00 && pBuffer[off+2]==0x00 && pBuffer[off+3]==0x01 &&
           pBuffer[off+4]!=0x67 && pBuffer[off+4]!=0x68 && pBuffer[off+4]!=0x06 //非SPS,PPS,I帧加强则认为是普通帧
           ){
            isFoundNF = TRUE;
            break;
        }
        
    }
    if(isFoundSPS){
        //SPS
        //必须找到第一个I帧才正式开始录像
        isAvailable = TRUE;
        //LOGE("录制一帧！SPS %d, %d", spsLoc, spsLen);
        MP4SetVideoProfileLevel(mp4FileHandle, 0x01);
        //MP4AddH264SequenceParameterSet(mp4FileHandle,mp4VideoTrackID,(uint8_t*)&buffer[spsLoc],12);
        MP4AddH264SequenceParameterSet(mp4FileHandle,mp4VideoTrackID,(uint8_t*)&buffer[spsLoc],spsLen);
    }
    
    if (!isAvailable) {
        return;
    }
    
    if(isFoundPPS){
        //PPS
        //LOGE("录制一帧！PPS %d, %d", ppsLoc, ppsLen);
        MP4AddH264PictureParameterSet(mp4FileHandle,mp4VideoTrackID,(uint8_t*)&buffer[ppsLoc],ppsLen);
    }
    if(isFoundIEX){
        //I帧加强信息
       // LOGE("录制一帧！I帧加强信息 %d, %d", iexLoc, iexLen);
    }
    if(isFoundNF){
        uint32_t* p = (uint32_t*)&buffer[off];
        *p = htonl(size-off-4);//大端,去掉头部四个字节
        MP4WriteSample(mp4FileHandle,mp4VideoTrackID,(uint8_t*)(&buffer[off]),size-off,m_vFrameDur/8000*90000,0,true);
        FoundNF = true;
        //LOGE("录制一帧！");
        m_vFrameDur = 0;
        return;
    } else {
        LOGE("录制一帧！ERR");
    }
}