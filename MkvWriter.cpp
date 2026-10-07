#include "MkvWriter.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
}
#include <memory>
#include <stdexcept>
#include <string>

MkvWriter::MkvWriter(const std::string& path, int width, int height, int fps, bool existsOkay, const std::string& libx264Preset, const std::string& libx264CRF) :
	m_frameWidth{ width }, m_frameHeight{ height } {

	// check if file exists
	// ENSURE THAT HEIGHT IS EVEN FOR H264 CODEC

	m_expectedFrameBufferSize = width * height * 3;

	try {
		AVFormatContext* rawFormatCtx{ nullptr };
		if (int ret = avformat_alloc_output_context2(&rawFormatCtx, nullptr, "matroska", path.c_str()); ret < 0) {
			throw_av_error("Could not allocate AVFormatContext for 'matroska'", ret);
		}
		m_formatCtx.reset(rawFormatCtx);

		const AVCodec* codec = avcodec_find_encoder(m_videoCodecID);
		if (!codec) throw std::runtime_error("Could not find encoder");

		m_videoCodecCtx.reset(avcodec_alloc_context3(codec));
		if (!m_videoCodecCtx) throw std::runtime_error("Could not allocate AVCodecContext for the encoder");
		m_videoCodecCtx->width = width;
		m_videoCodecCtx->height = height;
		m_videoCodecCtx->pix_fmt = m_outputPixelFormat;
		m_videoCodecCtx->time_base = { 1, fps };
		m_videoCodecCtx->framerate = { fps, 1 };

		//m_videoCodecCtx->bit_rate = 4000000; // not needed with libx264 CRF
		//m_videoCodecCtx->gop_size = 12; // can choose to set these later if I want
		//m_videoCodecCtx->max_b_frames = 2; // can choose to set these later if I want

		// set out-of-band global headers for MKV
		if (m_formatCtx->oformat->flags & AVFMT_GLOBALHEADER)
			m_videoCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

		// extra options for libx264
		av_opt_set(m_videoCodecCtx->priv_data, "preset", libx264Preset.c_str(), 0);
		av_opt_set(m_videoCodecCtx->priv_data, "crf", libx264CRF.c_str(), 0); // 0 = lossless, 51 = worst quality

		if (int ret = avcodec_open2(m_videoCodecCtx.get(), codec, nullptr); ret < 0) {
			throw_av_error("Could not open AVCodecContext", ret);
		}

		addVideoStream();

		m_swsCtx.reset(sws_getContext(width, height, m_inputPixelFormat, width, height, m_outputPixelFormat, SWS_BILINEAR, nullptr, nullptr, nullptr));
		if (!m_swsCtx) throw std::runtime_error("Failed to create SwsContext for pixel format conversion");


		if (int ret = avio_open(&(m_formatCtx->pb), path.c_str(), AVIO_FLAG_WRITE); ret < 0) {
			throw_av_error("Could not open output file", ret);
		}
		if (int ret = avformat_write_header(m_formatCtx.get(), nullptr); ret < 0) {
			throw_av_error("Could not write header to output file", ret);
		}
		m_headerWritten = true;

		m_rgbFrame.reset(av_frame_alloc());
		m_convertedFrame.reset(av_frame_alloc());
		m_packet.reset(av_packet_alloc());
	}
	catch (const std::exception& e) {
		std::fprintf(stderr, "Error occurred while initializing MkvWriter: %s\n", e.what());
		m_formatCtx.reset();
		m_videoCodecCtx.reset();
		m_audioCodecCtx.reset();
		m_swsCtx.reset();
		m_packet.reset();
		m_rgbFrame.reset();
		m_convertedFrame.reset();
		throw;
	}
}

MkvWriter::~MkvWriter() {
	finalizeFile();
}

int MkvWriter::finalizeFile() {
	if (m_trailerWritten || !m_headerWritten) return 0;

	try {
		writeVideoAVFrame(nullptr, -1);
	}
	catch (const std::exception& e) {
		// log ?
		// if this errors, then it will just be missing the last frames from the buffer
	}
	int writeTrailerResult = av_write_trailer(m_formatCtx.get());
	if (writeTrailerResult >= 0) m_trailerWritten = true;
	return writeTrailerResult;
}

void MkvWriter::addVideoStream() {
	m_videoStream = avformat_new_stream(m_formatCtx.get(), nullptr);
	if (!m_videoStream) throw std::runtime_error("Could not create new video stream");
	if (int ret = avcodec_parameters_from_context(m_videoStream->codecpar, m_videoCodecCtx.get()); ret < 0) {
		throw_av_error("Could not copy codec parameters to video stream", ret);
	}
	m_videoStream->time_base = m_videoCodecCtx->time_base;
}
void MkvWriter::addAudioStream() {
	m_audioStream = avformat_new_stream(m_formatCtx.get(), nullptr);
	if (!m_audioStream) throw std::runtime_error("Could not create new audio stream");
	if (int ret = avcodec_parameters_from_context(m_audioStream->codecpar, m_audioCodecCtx.get()); ret < 0) {
		throw_av_error("Could not copy codec parameters to audio stream", ret);
	}
	m_audioStream->time_base = m_audioCodecCtx->time_base;
}

int MkvWriter::writeVideoBufferFrame(const char* rgbBuffer, int rgbBufferSize, int64_t pts) {
	if (rgbBufferSize != m_expectedFrameBufferSize) throw std::runtime_error("Unexpected pixel data buffer size for pts " + pts);
	int numFramesWritten{ 0 };
	
	m_rgbFrame->width = m_frameWidth;
	m_rgbFrame->height = m_frameHeight;
	m_rgbFrame->format = m_inputPixelFormat;
	uint8_t* ppmData[4];
	int ppmLinesize[4];
	if (int ret = av_image_fill_arrays(ppmData, ppmLinesize, reinterpret_cast<const uint8_t*>(rgbBuffer), m_inputPixelFormat, m_frameWidth, m_frameHeight, 1); ret < 0)
		throw_av_error("Could not fill pointers for PPM data", ret);
	if (int ret = av_frame_get_buffer(m_rgbFrame.get(), 0); ret < 0) throw_av_error("Could not allocate buffer for raw RGB frame", ret);
	av_image_copy(m_rgbFrame->data, m_rgbFrame->linesize, ppmData, ppmLinesize, m_inputPixelFormat, m_frameWidth, m_frameHeight);

	m_convertedFrame->width = m_frameWidth;
	m_convertedFrame->height = m_frameHeight;
	m_convertedFrame->format = m_outputPixelFormat;
	if (int ret = av_frame_get_buffer(m_convertedFrame.get(), 0); ret < 0) throw_av_error("Could not allocate buffer for converted frame", ret);
	if (int ret = sws_scale_frame(m_swsCtx.get(), m_convertedFrame.get(), m_rgbFrame.get()); ret < 0) throw_av_error("Could not convert frame from RGB to YUV420P", ret);

	numFramesWritten = numFramesWritten + writeVideoAVFrame(m_convertedFrame.get(), pts);

	av_frame_unref(m_rgbFrame.get());
	av_frame_unref(m_convertedFrame.get());

	return numFramesWritten;
}

int MkvWriter::writeVideoAVFrame(AVFrame* frame, int64_t pts) {
	int numFramesWritten{ 0 };

	if (frame != nullptr && (frame->format != m_videoCodecCtx->pix_fmt || frame->width != m_videoCodecCtx->width || frame->height != m_videoCodecCtx->height)) {
		throw std::runtime_error("Frame format or dimensions do not match the codec context");
	}
	if (frame) frame->pts = pts;

	bool frameAccepted = false;
	for (int attempt = 0; attempt < 2 && !frameAccepted; ++attempt) {
		int ret = avcodec_send_frame(m_videoCodecCtx.get(), frame);
		if (ret == 0) frameAccepted = true;
		// AVERORR(EAGAIN) means that the encoder's buffer is full and must be drained before it can accept more frames. Continue on and try again
		else if (ret != AVERROR(EAGAIN)) throw_av_error("Could not send frame to video codec", ret);

		// drain the encoder and write all available packets to the output file
		numFramesWritten += drainVideoEncoder();
	}
	// if the frame was not accepted after two attempts, throw an error
	if (!frameAccepted) throw std::runtime_error("Video encoder still full after draining, frame could not be accepted");
	
	return numFramesWritten;
}

int MkvWriter::drainVideoEncoder() {
	int numFramesWritten{ 0 };
	while (true) {
		if (int ret = avcodec_receive_packet(m_videoCodecCtx.get(), m_packet.get());
			ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return numFramesWritten;
		else if (ret < 0) throw_av_error("Error encoding frame in video codec", ret);

		av_packet_rescale_ts(m_packet.get(), m_videoCodecCtx->time_base, m_videoStream->time_base);
		m_packet->stream_index = m_videoStream->index;
		int writeReturn = av_interleaved_write_frame(m_formatCtx.get(), m_packet.get());
		av_packet_unref(m_packet.get());
		if (writeReturn < 0) throw_av_error("Error writing frame to file", writeReturn);

		++numFramesWritten;
	}
	return numFramesWritten;
}