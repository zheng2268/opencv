#ifndef _AW_G2D_H_
#define _AW_G2D_H_



#include <bits/stdint-intn.h>
class AWG2d{
public:
    AWG2d();
    ~AWG2d();
    int32_t NV21ToBGR(int32_t srcFd, int32_t srcWidth,
                        int32_t srcHeight, int32_t dstFd,
                        int32_t dstWidth, int32_t dstHeight);

private:
    int32_t _g2dFd = -1;

};


#endif //_AW_G2D_H_