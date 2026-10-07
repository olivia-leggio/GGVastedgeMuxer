#ifndef VIDEO_UTILS_H
#define VIDEO_UTILS_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libswscale/swscale.h>
}
#include <memory>
#include <string>

struct InputFormatCtxDeleter { void operator()(AVFormatContext* p) const { if (p) avformat_close_input(&p); } };
struct CodecCtxDeleter { void operator()(AVCodecContext* p) const { if (p) avcodec_free_context(&p); } };
struct FrameDeleter { void operator()(AVFrame* p) const { if (p) av_frame_free(&p); } };
struct PacketDeleter { void operator()(AVPacket* p) const { if (p) av_packet_free(&p); } };
struct FilterGraphDeleter { void operator()(AVFilterGraph* p) const { if (p) avfilter_graph_free(&p); } };
struct SwsCtxDeleter { void operator()(SwsContext* p) const { if (p) sws_freeContext(p); } };
struct OutputFormatCtxDeleter {
	void operator()(AVFormatContext* p) const {
		if (p) {
			avio_closep(&p->pb);
			avformat_free_context(p);
} } };

struct DecoderState {
	std::unique_ptr<AVFormatContext, InputFormatCtxDeleter> format_ctx;
	std::unique_ptr<AVCodecContext, CodecCtxDeleter> codec_ctx;
	int video_stream_index{ -1 };
};
struct FilterState {
	std::unique_ptr<AVFilterGraph, FilterGraphDeleter> graph;
	AVFilterContext* buffersrc_ctx{ nullptr };
	AVFilterContext* buffersink_ctx{ nullptr };
};

struct FrameTransferState {
	std::unique_ptr<AVCodecContext, CodecCtxDeleter> codec_ctx;
	std::unique_ptr<SwsContext, SwsCtxDeleter> sws_ctx;
	std::unique_ptr<AVFrame, FrameDeleter> in_frame;
	std::unique_ptr<AVFrame, FrameDeleter> out_frame;
};

[[noreturn]] void throw_av_error(const std::string& what, int err);

DecoderState open_input_file(const std::string& path);

void print_file_info(const DecoderState& decoder);

void print_frames_info(const DecoderState& decoder, int max_frames);

void print_frame_ascii(const AVFrame* frame, AVRational time_base, bool clear_screen = true);

void save_frame_to_png(FrameTransferState& fts, std::string& path);

void save_frame_to_ppm(FrameTransferState& fts, std::string& path);

#endif // VIDEO_UTILS_H