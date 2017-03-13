
#include "Mp4Recorder.h"


class STREAM_THREAD : public QThread
{
    Q_OBJECT
public:
    Mp4Recorder *mp4Recoder;
    //SPS PPS
    unsigned char sps_pps[128];
	int videoWidth;			//视频宽
    int videoHeight;		//视频高
    int frameRatio;         //视频帧率
    const QString VIDEO_PATH = "/temp/video/";
	//TODO......define other para/fun
}

void STREAM_THREAD::startRecord()
{
    if(!this->isRunning())
        return ;
    qDebug() << "开始录像";
    QString fileName;
    fileName.append(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));

    fileName.append(VIDEO_PATH);

    fileName.append(QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss").append(".mp4"));

    Mp4Recorder *recorder=new Mp4Recorder();
    int ret=recorder->startRecord(fileName.toStdString().c_str(),
                                  this->videoWidth,
                                  this->videoHeight,
                                  this->frameRatio,
                                  sps_pps);
    if(ret>=0){
        this->mp4Recoder=recorder;
    }
}

void STREAM_THREAD::stopRecord()
{
    qDebug() << "停止录像";
    if(this->mp4Recoder == NULL){
        return ;
    }
    Mp4Recorder * recorder=this->mp4Recoder;
    this->mp4Recoder=NULL;
    //bool isAvailable=recorder->isAvailable;
    recorder->stopRecord();
    delete recorder;
    //return isAvailable;
}

void STREAM_THREAD::run()
{
	while (***) {
		//TODO....get stream
		if (packet.av_type == STREAM_DATA_VIDEO) {
			//TODO...other operation ,such as decode...
			//如果当前处于录像时间,那么录制视频数据
			if(this->mp4Recoder!=NULL){
				gettimeofday(&tv,NULL);
				this->mp4Recoder->writeVideoData(packet.data, packet.size,(tv.tv_sec * 1000 + tv.tv_usec / 1000-oldVideoTimeStamp));
			}
			oldVideoTimeStamp = tv.tv_sec * 1000 + tv.tv_usec / 1000;
			free(buffer);
		} else if (packet.av_type == STREAM_DATA_AUDIO) {
			//TODO...other operation ,such as decode...
			//如果当前处于录像时间,那么录制音频数据
			if(this->mp4Recoder!=NULL){
				//获取当前的时间
				gettimeofday(&tv,NULL);
				//录制的时候，将时间差记录
				this->mp4Recoder->writeAudioData(packet.data, packet.size, tv.tv_sec * 1000 + tv.tv_usec / 1000-oldAudioTimeStamp);
				//保存当前的时间
				oldAudioTimeStamp = tv.tv_sec * 1000 + tv.tv_usec / 1000;
			}
		}
	}

	//TODO...free resource
}

STREAM_THREAD::STREAM_THREAD()
{
	mp4Recoder = NULL;
}