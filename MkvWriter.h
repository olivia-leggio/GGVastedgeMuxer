#ifndef MKV_WRITER_H
#define MKV_WRITER_H

#include "VideoUtils.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <memory>
#include <string>

/** @brief Class for creating an MKV file and writing AVFrame objects to it */
class MkvWriter {
public:
	// Opens the file, sets up format and codec contexts, and writes the header
	MkvWriter(const std::string& path, int width, int height, int fps, bool existsOkay = false, const std::string& libx264Preset = "medium", const std::string& libx264CRF = "23");

	~MkvWriter();

	// Writes the trailer and closes the file
	int finalizeFile();

	// writes a frame from buffered RGB pixel data
	// expects a buffer of 3 bytes per pixel of size (width * height * 3)
	int writeVideoBufferFrame(const char* rgbBuffer, int rgbBufferSize, int64_t pts);

	// writes an AVFrame to the file
	int writeVideoAVFrame(AVFrame* frame, int64_t pts);
private:
	// drain the encoder and write all available packets to the output file. Returns the number of frames written.
	int drainVideoEncoder();

	void addVideoStream();
	void addAudioStream();

	bool m_headerWritten{ false };
	bool m_trailerWritten{ false };

	int m_frameWidth{};
	int m_frameHeight{};
	int m_expectedFrameBufferSize{};
	AVPixelFormat m_inputPixelFormat = AV_PIX_FMT_RGB24;
	AVPixelFormat m_outputPixelFormat = AV_PIX_FMT_YUV420P;

	AVCodecID m_videoCodecID = AV_CODEC_ID_H264;

	std::unique_ptr<AVFormatContext, OutputFormatCtxDeleter> m_formatCtx;
	std::unique_ptr<AVCodecContext, CodecCtxDeleter> m_videoCodecCtx;
	std::unique_ptr<AVCodecContext, CodecCtxDeleter> m_audioCodecCtx;
	std::unique_ptr<SwsContext, SwsCtxDeleter> m_swsCtx;
	std::unique_ptr<AVPacket, PacketDeleter> m_packet;
	std::unique_ptr<AVFrame, FrameDeleter> m_rgbFrame;
	std::unique_ptr<AVFrame, FrameDeleter> m_convertedFrame;

	AVStream* m_videoStream{ nullptr }; // no wrapper because freeing is handled by the parent format context
	AVStream* m_audioStream{ nullptr }; // no wrapper because freeing is handled by the parent format context
};
#endif // MKV_WRITER_H