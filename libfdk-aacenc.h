//
// Created by itisyang.
//

#ifndef PPSTRONGVIDEO_LIBFDK_AACENC_H
#define PPSTRONGVIDEO_LIBFDK_AACENC_H

#include <sys/types.h>

typedef struct AACEncodeContext AACEncodeContext;

/**
 * 初始化aac编码器
 * @param ectx          上下文的指针
 * @param channels      声道数
 * @param samplerate    采样率
 * @param nb_samples    每一个音频帧的长度
 *
 * @return  返回0为成功,其它失败
 *
 *
 */
int aac_encode_init(AACEncodeContext ** ectx, int channels, int samplerate, int nb_samples);

/**
 * 编码帧
 * @param ctx       AAC编码的上下文
 * @param inData    需要编码的音频帧数据
 * @return  返回0成功,其它失败
 */
int aac_encode_frame(AACEncodeContext *ctx, u_int8_t *inData);

/**
 * 获取当前编码之后的帧数据大小
 * @param ctx AAC编码的上下文
 * @return  编码之后的帧大小
 */
int aac_get_out_size(AACEncodeContext *ctx);

/**
 * 获取编码之后的数据帧的指针
 * @param ctx   AAC编码的上下文
 * @return  数据指针
 */
u_int8_t *aac_get_out_buffer(AACEncodeContext *ctx);

/**
 * 释放AAC编码上下文
 * 释放完成以后会对ctx置空
 */
void aac_free_context(AACEncodeContext **ctx);

#endif //PPSTRONGVIDEO_LIBFDK_AACENC_H
