#ifndef _CAP_AW_CSI_H_
#define _CAP_AW_CSI_H_


#include "opencv2/core/cvdef.h"
#include "precomp.hpp"
#include <bits/stdint-intn.h>
#include <memory>
#include <mutex>
#include <string>

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <limits>

#include <poll.h>
#include <vector>

#ifdef HAVE_CAMV4L2
#include "aw_dmabuf.h"
#include <linux/videodev2.h>
#include "aw_g2d.h"
#include "AWIspApi.h"
#include "aw_dmabuf_pool.h"
#include "memory"
#include "map"
#include "array"
#endif

#ifdef HAVE_VIDEOIO
// NetBSD compatibility layer with V4L2
#include <sys/videoio.h>
#endif

/* Defaults - If your board can do better, set it here.  Set for the most common type inputs. */
#define DEFAULT_V4L_WIDTH  640
#define DEFAULT_V4L_HEIGHT 480
#define DEFAULT_V4L_FPS 30

#define MAX_CAMERAS 8

// default and maximum number of V4L buffers, not including last, 'special' buffer
#define MAX_V4L_BUFFERS 10
#define DEFAULT_V4L_BUFFERS 4

// types of memory in 'special' buffer
enum {
    MEMORY_ORIG = 0, // Image data in original format.
    MEMORY_RGB  = 1, // Image data converted to RGB format.
};

// if enabled, then bad JPEG warnings become errors and cause NULL returned instead of image
#define V4L_ABORT_BADJPEG

namespace cv {

struct Buffer{
    std::shared_ptr<AWDmaBuf> AwDmaBuf;
    v4l2_buffer v4L2Buffer;
};

struct CvCaptureAW_CSI CV_FINAL : public IVideoCapture {
public:
    CvCaptureAW_CSI();
    ~CvCaptureAW_CSI() CV_OVERRIDE;
    int getCaptureDomain() /*const*/ CV_OVERRIDE { return cv::CAP_V4L; }
    bool isOpened() const CV_OVERRIDE;
    virtual double getProperty(int) const CV_OVERRIDE;
    virtual bool setProperty(int, double) CV_OVERRIDE;
    virtual bool grabFrame() CV_OVERRIDE;
    virtual bool retrieveFrame(int, OutputArray) CV_OVERRIDE;

    bool open( int index );
private:
    bool initCapture();
    void closeDevice();
    bool read_frame_v4l2();

    bool streaming(bool startStream);
    bool v4l2_reset();
    bool icvSetFrameSize(int _width, int _height);
    bool setFps(int fps);

    int32_t _cameraIndex = -1;
    int32_t _cameraFd = -1;
    cv::String _deviceName;
    bool _firstCapture;
    bool _streamStarted;
    int32_t _width = 0;
    int32_t _height = 0;
    int32_t _widthSet = 0;
    int32_t _heightSet = 0;
    int32_t _fps = 0;
    int32_t _nplanes = 0;
    std::mutex _apiLocks;

    struct v4l2_input inp {};           /* select the current video input */
    struct v4l2_streamparm parms {};    /* set streaming parameters */
    struct v4l2_format fmt {};          /* try a format */
    struct v4l2_requestbuffers req {};  /* Initiate Memory Mapping or User Pointer I/O */
    struct v4l2_buffer buf {};          /* Query the status of a buffer */


    AWIspApi *_awIspApi = nullptr;
    int32_t _ispId = -1;

    std::unique_ptr<AWG2d> _awG2d;
    std::unique_ptr<AWDmaBufPool> _nv21DmaBufPool;
    std::unique_ptr<AWDmaBufPool> _bgrDmaBufPool;
    std::shared_ptr<AWDmaBuf> _bgrDmaBuf;
    int32_t _bgrFrameSize = 0;
    int32_t _nv21FrameSize = 0;
    int32_t _bufferIndex = -1;

    std::array<Buffer, DEFAULT_V4L_BUFFERS> _buffers;

    Mat _cvFrame;
};


Ptr<IVideoCapture> create_awcsi_capture_cam(int index)
{
    Ptr<CvCaptureAW_CSI> ret = makePtr<CvCaptureAW_CSI>();
    if (ret->open(index))
        return ret;
    return NULL;
}

}

#endif //CAP_AW_CSI_H