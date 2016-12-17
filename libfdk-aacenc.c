//
// Created by itisyang.
//

#include "libfdk-aacenc.h"
#include "fdk-aac/aacenc_lib.h"
#include "fdk-aac/FDK_audio.h"

#include <sys/param.h>
#include <stdio.h>
#include <memory.h>
#include <android/log.h>
#include <pthread.h>

#define DLog(...) __android_log_print(ANDROID_LOG_INFO, "debug-fdk_aac", __VA_ARGS__)

typedef struct AACEncodeContext {
    HANDLE_AACENCODER  *encoder;
    int channels;
    int nb_samples;
    int frame_size;
    int initial_padding;
    uint8_t outData[8192];
    int outSize;
} AACEncodeContext;

int aac_encode_init(AACEncodeContext ** ectx, int channels, int samplerate, int nb_samples) {
    HANDLE_AACENCODER *encoder = (HANDLE_AACENCODER*)calloc(sizeof(HANDLE_AACENCODER), 1);
    AACENC_InfoStruct info = {0};
    CHANNEL_MODE mode;
    AACENC_ERROR err;
    int sce = 0, cpe = 0;
    if ((err = aacEncOpen(encoder, 0, channels)) != AACENC_OK) {
        DLog("打开fdk-aac的编码器失败!");
        goto error;
    }
    if ((err = aacEncoder_SetParam(*encoder, AACENC_AOT, AOT_AAC_LC)) != AACENC_OK) {
        DLog("不能设置AOT");
        goto error;
    }
    if ((err = aacEncoder_SetParam(*encoder, AACENC_SAMPLERATE, samplerate)) != AACENC_OK) {
        DLog("不能设置采样率:%d", samplerate);
        goto error;
    }
    switch (channels) {
        case 1:mode = MODE_1;          sce = 1;    cpe = 0;  break;
        case 2:mode = MODE_2;          sce = 0;    cpe = 1;  break;
        case 3:mode = MODE_1_2;        sce = 1;    cpe = 1;  break;
        case 4:mode = MODE_1_2_1;      sce = 2;    cpe = 1;  break;
        case 5:mode = MODE_1_2_2;      sce = 1;    cpe = 2;  break;
        case 6:mode = MODE_1_2_2_1;    sce = 2;    cpe = 2;  break;
        default:
            DLog("无法设置声道模式");
            return -1;
    }
    if ((err = aacEncoder_SetParam(*encoder, AACENC_CHANNELMODE, mode)) != AACENC_OK) {
        DLog("设置声道模式失败");
        goto error;
    }

    if ((err = aacEncoder_SetParam(*encoder, AACENC_CHANNELORDER, 1)) != AACENC_OK) {
        DLog("不能设置wav声道排列 %d", mode);
        goto error;
    }

    int bitrate = (96 * sce + 128 * cpe) * samplerate / 44;
    if ((err = aacEncoder_SetParam(*encoder, AACENC_BITRATE, bitrate)) != AACENC_OK) {
        DLog("不能设置码率 %d", bitrate);
        goto error;
    }

    if ((err = aacEncoder_SetParam(*encoder, AACENC_TRANSMUX, 0)) != AACENC_OK) {
        DLog("不能够设置transmux format");
        goto error;
    }

    if ((err = aacEncEncode(*encoder, NULL, NULL, NULL, NULL)) != AACENC_OK) {
        DLog("初始化编码器失败!");
        goto error;
    }

    if ((err = aacEncInfo(*encoder, &info)) != AACENC_OK) {
        DLog("获取编码器信息失败!");
        goto error;
    }

    AACEncodeContext *_ectx = (AACEncodeContext*)calloc(sizeof(AACEncodeContext), 1);
    _ectx->encoder = encoder;
    _ectx->frame_size = info.frameLength;
    _ectx->initial_padding = info.encoderDelay;
    _ectx->channels = channels;
    _ectx->nb_samples = nb_samples;
    (*ectx) = _ectx;

    return 0;

error:
    aacEncClose((HANDLE_AACENCODER *)(*encoder));
    if (encoder)free(encoder);
    return err;
}

int aac_encode_frame(AACEncodeContext *ctx, u_int8_t *inData) {
    if (!ctx || !inData) {
        DLog("传入的上下文或者帧数据为空");
        return -1;
    }
    AACENC_BufDesc in_buf = {0}, out_buf = {0};
    AACENC_InArgs in_args = {0};
    AACENC_OutArgs out_args = {0};
    int in_buffer_identifier = IN_AUDIO_DATA;
    int in_buffer_size, in_buffer_element_size;
    int out_buffer_identifier = OUT_BITSTREAM_DATA;
    int out_buffer_size, out_buffer_element_size;

    void *in_ptr, *out_ptr;
    AACENC_ERROR err;
    in_ptr = inData;
    in_buffer_size = 2 * ctx->channels * ctx->nb_samples;
    in_buffer_element_size = 2;

    in_args.numInSamples = ctx->channels * ctx->nb_samples;
    in_buf.numBufs = 1;
    in_buf.bufs = &in_ptr;
    in_buf.bufferIdentifiers = &in_buffer_identifier;
    in_buf.bufSizes = &in_buffer_size;
    in_buf.bufElSizes = &in_buffer_element_size;

    ctx->outSize = 8192>(768 * ctx->channels)?8192:(768 * ctx->channels);

    out_ptr = ctx->outData;
    out_buffer_size = ctx->outSize;
    out_buffer_element_size = 1;
    out_buf.numBufs = 1;
    out_buf.bufs = &out_ptr;
    out_buf.bufferIdentifiers = &out_buffer_identifier;
    out_buf.bufSizes = &out_buffer_size;
    out_buf.bufElSizes = &out_buffer_element_size;

    if (!ctx)DLog("ctx NULL");
    if (!(ctx->encoder))DLog("encoder NULL");
    if (!inData)DLog("inData NULL");

    if ((err = aacEncEncode(*ctx->encoder, &in_buf, &out_buf, &in_args, &out_args)) != AACENC_OK) {
        DLog("不能编码帧!");
        return err;
    }
    if (!out_args.numOutBytes) {    //可能编码帧失败,或者编码出来的帧为空
        return -1;
    }

    ctx->outSize = out_args.numOutBytes;
    return 0;
}

int aac_get_out_size(AACEncodeContext *ctx) {
    if (!ctx)return 0;
    return ctx->outSize;
}

u_int8_t *aac_get_out_buffer(AACEncodeContext *ctx) {
    if (!ctx)return NULL;
    return ctx->outData;
}

void aac_free_context(AACEncodeContext **ctx) {
    if (!(*ctx)) {
        return;
    }
    AACEncodeContext *ctx_ = *ctx;
    if (ctx_->encoder) {
        aacEncClose(ctx_->encoder);
        free(ctx_->encoder);
    }
    free(ctx_);
    (*ctx) = NULL;
}