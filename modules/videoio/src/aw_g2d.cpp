#include "aw_g2d.h"
#include <cstdio>
#include <fcntl.h>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include "g2d_driver_enh.h"
#include "string.h"
#include <sys/ioctl.h>
#include "unistd.h"

#define G2D_DEV_NAME "/dev/g2d"

AWG2d::AWG2d() {
    _g2dFd = open(G2D_DEV_NAME, O_RDWR, 0);
    if(_g2dFd < 0){
        throw std::runtime_error("Open /dev/g2d failed");
    }
}

AWG2d::~AWG2d() {
    if( _g2dFd >= 0 ){
        close(_g2dFd);
    }
}

int32_t AWG2d:: NV21ToBGR(int32_t srcFd, int32_t srcWidth,
                    int32_t srcHeight, int32_t dstFd,
                    int32_t dstWidth, int32_t dstHeight) {

    g2d_blt_h blit;
    memset(&blit, 0, sizeof(g2d_blt_h));

    blit.flag_h = G2D_BLT_NONE_H;
    blit.src_image_h.use_phy_addr = 0;
    blit.src_image_h.fd = srcFd;
    blit.src_image_h.format = G2D_FORMAT_YUV420UVC_V1U1V0U0;

    blit.src_image_h.width = srcWidth;
    blit.src_image_h.height = srcHeight;
    blit.src_image_h.clip_rect.x = 0;
    blit.src_image_h.clip_rect.y = 0;
    blit.src_image_h.clip_rect.w = srcWidth;
    blit.src_image_h.clip_rect.h = srcHeight;

    blit.src_image_h.mode = G2D_PIXEL_ALPHA;
    blit.src_image_h.alpha = 255;
    blit.src_image_h.color = 0xee8899;
    blit.src_image_h.align[0] = 0;
    blit.src_image_h.align[1] = blit.src_image_h.align[0];
    blit.src_image_h.align[2] = blit.src_image_h.align[0];
    blit.src_image_h.bbuff = 1;

    blit.dst_image_h.use_phy_addr = 0;
    blit.dst_image_h.fd = dstFd;
    blit.dst_image_h.format = G2D_FORMAT_BGR888;
    blit.dst_image_h.width = dstWidth;
    blit.dst_image_h.height = dstHeight;
    blit.dst_image_h.clip_rect.x = 0;
    blit.dst_image_h.clip_rect.y = 0;
    blit.dst_image_h.clip_rect.w = dstWidth;
    blit.dst_image_h.clip_rect.h = dstHeight;

    blit.dst_image_h.mode = G2D_PIXEL_ALPHA;
    blit.dst_image_h.alpha = 255;
    blit.dst_image_h.color = 0xee8899;
    blit.dst_image_h.align[0] = 0;
    blit.dst_image_h.align[1] = blit.dst_image_h.align[0];
    blit.dst_image_h.align[2] = blit.dst_image_h.align[0];
    blit.dst_image_h.laddr[1] = (uintptr_t) 0;
    blit.dst_image_h.laddr[2] = (uintptr_t) 0;
    blit.dst_image_h.bbuff = 1;

    if (ioctl(_g2dFd, G2D_CMD_BITBLT_H, (uintptr_t)(&blit)) < 0) {
        printf("ERROR: %s G2D_CMD_BITBLT_H Rotate failed\n", __FUNCTION__);
        return false;
    }
    return true;
}
