#include "cap_aw_csi.h"
#include "opencv2/core/cvdef.h"
#include "opencv2/core/hal/interface.h"
#include "opencv2/core/mat.hpp"
#include "opencv2/core/types.hpp"
#include "opencv2/core/types_c.h"
#include "opencv2/core/utils/logger.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/videoio.hpp"
#include <bits/stdint-intn.h>
#include <bits/stdint-uintn.h>
#include <bits/types/FILE.h>
#include <cstdio>
#include <cstring>
#include <linux/videodev2.h>
#include <mutex>
#include <sys/ioctl.h>

namespace cv {

#define V4L2_MODE_VIDEO			0x0002

#define ALIGN(v, align) (((v) + (align - 1)) & (~(align - 1)))

CvCaptureAW_CSI::CvCaptureAW_CSI() {

}

CvCaptureAW_CSI::~CvCaptureAW_CSI() {
    closeDevice();
}

bool CvCaptureAW_CSI::icvSetFrameSize(int width, int height)
{
    if (width > 0)
        _widthSet = width;

    if (height > 0)
        _heightSet = height;

    /* two subsequent calls setting WIDTH and HEIGHT will change
       the video size */
    if (_widthSet <= 0 || _heightSet <= 0)
        return true;

    _width = _widthSet;
    _height = _heightSet;
    _width = ALIGN(_width, 16);
    _height = ALIGN(_height, 16);
    _widthSet = _heightSet = 0;
    return v4l2_reset();
}

bool CvCaptureAW_CSI::setFps(int value)
{
    if (!isOpened())
        return false;

    parms.parm.capture.reserved[0] = 0;
    parms.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    parms.parm.capture.capturemode = V4L2_MODE_VIDEO;
    parms.parm.capture.timeperframe.numerator = 1;
    parms.parm.capture.timeperframe.denominator = value;
    if (ioctl(_cameraFd, VIDIOC_S_PARM, &parms) < 0) {
        CV_LOG_DEBUG(NULL, "Setting streaming parameters failed, numerator:" << parms.parm.capture.timeperframe.numerator
            << ", denominator:" << parms.parm.capture.timeperframe.denominator);
        return false;
    }
    _fps = value;
    return true;
}

double CvCaptureAW_CSI::getProperty(int property_id) const {
    switch (property_id) {
        case cv::CAP_PROP_FRAME_WIDTH:{
            return _width;
        }
        case cv::CAP_PROP_FRAME_HEIGHT:{
            return _height;
        }
        case cv::CAP_PROP_FPS: {
            return _fps;
        }
        default:
        {
            return 0.0f;
        }
    }
    return 0.0f;
}

bool CvCaptureAW_CSI::setProperty(int property_id, double _value) {
    int value = cvRound(_value);
    switch (property_id) {
        case cv::CAP_PROP_FRAME_WIDTH:{
            return icvSetFrameSize(value, 0);
        }

        case cv::CAP_PROP_FRAME_HEIGHT:{
            return icvSetFrameSize(0, value);
        }

        case cv::CAP_PROP_FPS:{
            if (_fps == static_cast<int32_t>(value)){
                return true;
            }
            return setFps(value);
        }

        default:
            return false;
    }
    return false;
}

bool CvCaptureAW_CSI::v4l2_reset() {

    if( _streamStarted ){
        streaming(false);
        _streamStarted = false;
    }

    if( _awIspApi ){
        if( _ispId >= 0 ){
            _awIspApi->ispStop( _ispId );
        }
        DestroyAWIspApi(_awIspApi);
        _awIspApi = nullptr;
    }

    for(auto &buffer : _buffers){
        buffer.AwDmaBuf.reset();
    }

    _bgrDmaBuf.reset();
    _bgrDmaBufPool.reset();
    _nv21DmaBufPool.reset();


    return initCapture();
}


bool CvCaptureAW_CSI::retrieveFrame(int, OutputArray ret) {

    if( isOpened() == false ){
        return false;
    }

    if (_bufferIndex < 0) {
        _cvFrame.copyTo(ret);
        return true;
    }

    auto dmaBuf = _buffers[_bufferIndex].AwDmaBuf;
    auto v4l2Buf = _buffers[_bufferIndex].v4L2Buffer;
    struct v4l2_plane planes[_nplanes];
    memset(planes, 0, _nplanes * sizeof(struct v4l2_plane));
    v4l2Buf.m.planes = planes;

    dmaBuf->sync();
    _awG2d->NV21ToBGR(dmaBuf->fd(), _width, _height, _bgrDmaBuf->fd(), _width, _height);
    _bgrDmaBuf->sync();

    cv::Size bgrFrameSize(_width, _height);

    //直接从DMA缓冲区映射数据到内存
    uint8_t *bgrBufPtr = (uint8_t *)_bgrDmaBuf->map();
    if (!bgrBufPtr) {
        CV_LOG_ERROR(NULL, "Failed to map DMA buffer");
        return false;
    }

    try {
        cv::Mat bgrMat(_height, _width, CV_8UC3, bgrBufPtr);
        bgrMat.copyTo(ret);
    } catch (const cv::Exception& e) {
        CV_LOG_ERROR(NULL, "Error copying to output: " << e.what());
        _bgrDmaBuf->unmap();
        return false;
    }

    // 取消映射
    _bgrDmaBuf->unmap();

    for (int i = 0; i < _nplanes; i++)
    {
        v4l2Buf.m.planes[i].m.fd = dmaBuf->fd();
        v4l2Buf.m.planes[i].length = _nv21FrameSize;
    }

    // 将buffer返回到队列
    if (ioctl(_cameraFd, VIDIOC_QBUF, &v4l2Buf) != 0) {
        CV_LOG_DEBUG(NULL, "VIDEOIO(V4L2:" << _deviceName << "): failed VIDIOC_QBUF: errno=" << errno << " (" << strerror(errno) << ")");
    }

    _bufferIndex = -1;
    return true;
}
bool CvCaptureAW_CSI::open(int index){

    /* Select camera, or rather, V4L video source */
    if (index < 0) // Asking for the first device available
    {
        for (int autoindex = 0; autoindex < MAX_CAMERAS; ++autoindex)
        {
            _deviceName = cv::format("/dev/video%d", autoindex);
            /* Test using an open to see if this new device name really does exists. */
            int h = ::open(_deviceName.c_str(), O_RDONLY);
            if (h != -1)
            {
                ::close(h);
                index = autoindex;
                _cameraIndex = index;
                break;
            }
        }
        if (index < 0)
        {
            CV_LOG_WARNING(NULL, "VIDEOIO(V4L2): can't find camera device");
            _deviceName.clear();
            return false;
        }
    }
    else
    {
        _cameraIndex = index;
        _deviceName = cv::format("/dev/video%d", index);
    }
    CV_LOG_DEBUG(NULL, "VIDEOIO(V4L2:" << _deviceName << "): opening...");
    _firstCapture = true;
    _width = utils::getConfigurationParameterSizeT("OPENCV_VIDEOIO_V4L_DEFAULT_WIDTH", DEFAULT_V4L_WIDTH);
    _height = utils::getConfigurationParameterSizeT("OPENCV_VIDEOIO_V4L_DEFAULT_HEIGHT", DEFAULT_V4L_HEIGHT);
    _widthSet = _heightSet = 0;
    _fps = DEFAULT_V4L_FPS;

    _cameraFd = ::open(_deviceName.c_str(), O_RDWR /* required */ | O_NONBLOCK, 0);

    if (_cameraFd == -1){
        CV_LOG_DEBUG(NULL, "Open device: " << _deviceName << " failed");
        return false;
    }


    return initCapture();
}

bool CvCaptureAW_CSI::isOpened() const{
    return _cameraFd != -1;
}

bool CvCaptureAW_CSI::initCapture() {

    if (!isOpened())
        return false;

    _bgrFrameSize = _width * _height * 3;
    _nv21FrameSize = _width * _height * 3 / 2;

    _bgrDmaBufPool.reset(new AWDmaBufPool(_bgrFrameSize, DEFAULT_V4L_BUFFERS));
    _nv21DmaBufPool.reset(new AWDmaBufPool(_nv21FrameSize, DEFAULT_V4L_BUFFERS));
    _awG2d.reset(new AWG2d());

    /* 1.select the current video input */
    memset(&inp, 0, sizeof(inp));
    inp.index = _cameraIndex;
    if (ioctl(_cameraFd, VIDIOC_S_INPUT, &inp) < 0) {
        CV_LOG_DEBUG(NULL, "VIDIOC_S_INPUT failed! s_input:" << inp.index);
        close(_cameraFd);
        _cameraFd = -1;
        return -1;
    }

    /* 2.init aw isp */
    _awIspApi = CreateAWIspApi();

    /* 3.set streaming parameters */

    parms.parm.capture.reserved[0] = 0;
    parms.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    parms.parm.capture.capturemode = V4L2_MODE_VIDEO;
    parms.parm.capture.timeperframe.numerator = 1;
    parms.parm.capture.timeperframe.denominator = _fps;
    if (ioctl(_cameraFd, VIDIOC_S_PARM, &parms) < 0) {
        CV_LOG_DEBUG(NULL, "Setting streaming parameters failed, numerator:" << parms.parm.capture.timeperframe.numerator
            << ", denominator:" << parms.parm.capture.timeperframe.denominator);
        close(_cameraFd);
        _cameraFd = -1;
        return -1;
    }

    /* 4.set the data format */

    memset(&fmt, 0, sizeof(struct v4l2_format));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = _width;
    fmt.fmt.pix_mp.height = _height;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV21;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;

    if (ioctl(_cameraFd, VIDIOC_S_FMT, &fmt) < 0) {
        CV_LOG_DEBUG(NULL, " setting the data format failed!");
        close(_cameraFd);
        _cameraFd = -1;
        return -1;
    }
    _nplanes = fmt.fmt.pix_mp.num_planes;

    /* 5. request & query buffer */

    memset(&req, 0, sizeof(struct v4l2_requestbuffers));
    req.count = DEFAULT_V4L_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_DMABUF;
    if (ioctl(_cameraFd, VIDIOC_REQBUFS, &req) < 0) {
        CV_LOG_DEBUG(NULL, " VIDIOC_REQBUFS failed");
        close(_cameraFd);
        _cameraFd = -1;
        return -1;
    }


    for (uint32_t n_buffers = 0; n_buffers < req.count; ++n_buffers) {
        memset(&buf, 0, sizeof(struct v4l2_buffer));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_DMABUF;
        buf.index = n_buffers;
        buf.length = _nplanes;
        struct v4l2_plane planes[_nplanes];
        memset(planes, 0, _nplanes * sizeof(struct v4l2_plane));
        buf.m.planes = planes;
        if (ioctl(_cameraFd, VIDIOC_QUERYBUF, &buf) < 0) {
            CV_LOG_DEBUG(NULL, " VIDIOC_QUERYBUF error");
            close(_cameraFd);
            _cameraFd = -1;
            return -1;
        }
    }

    /* 6.Exchange a buffer with the driver */
    for (uint32_t n_buffers = 0; n_buffers < req.count; n_buffers++) {
        memset(&buf, 0, sizeof(struct v4l2_buffer));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_DMABUF;
        buf.index = n_buffers;
        buf.length = _nplanes;
        struct v4l2_plane planes[_nplanes];
        memset(planes, 0, _nplanes * sizeof(struct v4l2_plane));
        buf.m.planes = planes;
        auto dmaBuf = _nv21DmaBufPool->allocate();
        for (int i = 0; i < _nplanes; i++)
        {
            buf.m.planes[i].m.fd = dmaBuf->fd();
            buf.m.planes[i].length = _nv21FrameSize;
        }


        if (ioctl(_cameraFd, VIDIOC_QBUF, &buf) < 0) {
            CV_LOG_DEBUG(NULL, " VIDIOC_QBUF error");
            close(_cameraFd);
            _cameraFd = -1;
            return -1;
        }

        _buffers[n_buffers].AwDmaBuf = dmaBuf;
        _buffers[n_buffers].v4L2Buffer = buf;
    }
    _bgrDmaBuf = _bgrDmaBufPool->allocate();
    cv::Size bgrFrameSize(_height, _width);
    _cvFrame.create(bgrFrameSize, CV_8UC3);
    streaming(true);
    return true;
}

bool CvCaptureAW_CSI::streaming(bool startStream)
{
    if (startStream != _streamStarted)
    {
        if (!isOpened())
        {
            CV_Assert(_streamStarted == false);
            return !startStream;
        }

        enum v4l2_buf_type type= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if( startStream == true ){
            if (ioctl(_cameraFd, VIDIOC_STREAMON, &type) < 0) {
                CV_LOG_DEBUG(NULL, " VIDIOC_STREAMOFF error! " << strerror(errno));
                return false;
            }
            _ispId = _awIspApi->ispGetIspId(_cameraIndex);
            if (_ispId >= 0){
                _awIspApi->ispStart(_ispId);
            }

            _streamStarted = startStream;
            return true;
        }else{
            if (ioctl(_cameraFd, VIDIOC_STREAMOFF, &type) < 0) {
                CV_LOG_DEBUG(NULL, "VIDIOC_STREAMOFF error! " << strerror(errno));
                return false;
            }
            if (_ispId >= 0){
                _awIspApi->ispStop(_ispId);
                _ispId = -1;
            }
            _streamStarted = startStream;
            return true;
        }

        if (startStream)
        {
            CV_LOG_DEBUG(NULL, "VIDEOIO(V4L2:" << _deviceName << "): failed VIDIOC_STREAMON: errno=" << errno << " (" << strerror(errno) << ")");
        }
        return false;
    }
    return startStream;
}

bool CvCaptureAW_CSI::read_frame_v4l2()
{
    struct v4l2_buffer buf{};
    memset(&buf, 0, sizeof(struct v4l2_buffer));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_DMABUF;
    buf.length = _nplanes;
    struct v4l2_plane planes[_nplanes];
    memset(planes, 0, _nplanes * sizeof(struct v4l2_plane));
    buf.m.planes = planes;


    timeval tv{};
    gettimeofday(&tv, nullptr);
    static int param_v4l_select_timeout = (int)utils::getConfigurationParameterSizeT("OPENCV_VIDEOIO_V4L_SELECT_TIMEOUT", 10);
    tv.tv_sec = param_v4l_select_timeout;
    tv.tv_usec = 0;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(_cameraFd, &fds);
    int32_t ret = select(_cameraFd + 1, &fds, nullptr, nullptr, &tv);
    if (ret == -1) {
        return false;
    } else if (ret == 0) {
        CV_LOG_DEBUG(NULL, "camera " << _cameraFd <<  " select timeout,end capture thread!");
        return false;
    }

    /* dqbuf */
    if (ioctl(_cameraFd, VIDIOC_DQBUF, &buf) < 0 ) {
        CV_LOG_DEBUG(NULL, "*****VIDIOC_DQBUF FAIL:*****" << strerror(errno));
        return false;
    }


    //We shouldn't use this buffer in the queue while not retrieve frame from it.
    _buffers[buf.index].v4L2Buffer = buf;
    _bufferIndex = buf.index;

    return true;
}

bool CvCaptureAW_CSI::grabFrame()
{
    if (_bufferIndex >= 0)
    {
        if (ioctl(_cameraFd, VIDIOC_QBUF, &_buffers[_bufferIndex].v4L2Buffer) < 0)
        {
            CV_LOG_DEBUG(NULL, "VIDEOIO(V4L2:" << _deviceName << "): failed VIDIOC_QBUF (buffer=" << _bufferIndex << "): errno=" << errno << " (" << strerror(errno) << ")");
        }
    }

    return read_frame_v4l2();
}

void CvCaptureAW_CSI::closeDevice()
{

    if( _streamStarted ){
        streaming(false);
        _streamStarted = false;
    }

    if( _awIspApi ){
        if( _ispId >= 0 ){
            _awIspApi->ispStop( _ispId );
        }
        DestroyAWIspApi(_awIspApi);
        _awIspApi = nullptr;
    }

    if( _cameraFd > 0 ){
        close(_cameraFd);
        _cameraFd = -1;
    }

    for(auto &buffer : _buffers){
        buffer.AwDmaBuf = nullptr;
    }
}

}

