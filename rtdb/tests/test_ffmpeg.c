/* ffmpeg_dyn 测试：只链 -ldl，不链 libav*
 * 注意：封装层首次调用 ffmpeg_libok() 时自动加载（无显式路径参数）。
 */
#include "test_common.h"

typedef struct AVCodec AVCodec;
typedef struct AVCodecContext AVCodecContext;

/* AV_CODEC_ID_H264 = 27（lavc 58/59/60/61 均一致） */
#define AV_CODEC_ID_H264 27

int ffmpeg_libok(void);
void ffmpeg_dyn_cleanup(void);
int ffmpeg_dyn_avcodec_major(void);

unsigned avutil_version(void);
const AVCodec *avcodec_find_decoder(int id);
AVCodecContext *avcodec_alloc_context3(const AVCodec *codec);
void avcodec_free_context(AVCodecContext **avctx);

int main(int argc, char **argv) {
    int failures = 0;
    const AVCodec *dec;
    AVCodecContext *ctx;
    (void)argc; (void)argv; /* ffmpeg 封装层无显式路径参数，靠 LD_LIBRARY_PATH/环境 */

    failures += CHECK(ffmpeg_libok() != 0);

    printf("avutil=%u (avcodec major=%d)\n", avutil_version(),
           ffmpeg_dyn_avcodec_major());
    failures += CHECK(avutil_version() > 0);

    dec = avcodec_find_decoder(AV_CODEC_ID_H264);
    failures += CHECK(dec != NULL);

    ctx = avcodec_alloc_context3(dec);
    failures += CHECK(ctx != NULL);
    if (ctx) avcodec_free_context(&ctx);

    /* 错误路径：不存在的编解码器 */
    failures += CHECK(avcodec_find_decoder(9999) == NULL);

    ffmpeg_dyn_cleanup();

    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}
