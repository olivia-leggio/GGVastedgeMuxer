#include "ExportFrames.h"
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

void export_png_frames(const DecoderState& decoder, const std::string& output_dir, int max_frames) {
	try {
		int ret{ 0 };
		std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());

		FrameTransferState fts{};
		fts.in_frame.reset(av_frame_alloc());

		int processed_frames{ 0 };
		auto drain_decoder = [&]() -> bool {
			while (true) {
				int ret = avcodec_receive_frame(decoder.codec_ctx.get(), fts.in_frame.get());
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) { return false; }
				if (ret < 0) throw_av_error("Error receiving frame from video decoder", ret);

				std::filesystem::path output_path = std::filesystem::path(output_dir);
				output_path.append(std::format("frame_{}.png", processed_frames + 1));
				std::string path_string = output_path.string();
				save_frame_to_png(fts, path_string);
				
				av_frame_unref(fts.in_frame.get());
				++processed_frames;
				if (processed_frames >= max_frames) return true;
			}
			};

		bool done = false;
		while (!done && av_read_frame(decoder.format_ctx.get(), packet.get()) >= 0) {
			if (packet->stream_index == decoder.video_stream_index) {
				ret = avcodec_send_packet(decoder.codec_ctx.get(), packet.get());
				if (ret < 0) throw_av_error("Cannot send packet to decoder codec", ret);
				done = drain_decoder();
			}
			av_packet_unref(packet.get());
		}
		if (!done) {
			avcodec_send_packet(decoder.codec_ctx.get(), nullptr);
			drain_decoder();
		}
	}
	catch (const std::exception& e) {
		std::fprintf(stderr, "Error occurred: %s\n", e.what());
	}
}

void export_ppm_frames(const DecoderState& decoder, const std::string& output_dir, int max_frames) {
	try {
		int ret{ 0 };
		std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());

		FrameTransferState fts{};
		fts.in_frame.reset(av_frame_alloc());

		int processed_frames{ 0 };
		auto drain_decoder = [&]() -> bool {
			while (true) {
				int ret = avcodec_receive_frame(decoder.codec_ctx.get(), fts.in_frame.get());
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return false;
				if (ret < 0) throw_av_error("Cannot receive frame from codec", ret);

				std::filesystem::path output_path = std::filesystem::path(output_dir);
				output_path.append(std::format("frame_{}.ppm", processed_frames + 1));
				std::string path_string = output_path.string();

				save_frame_to_ppm(fts, path_string);

				av_frame_unref(fts.in_frame.get());
				++processed_frames;
				if (processed_frames >= max_frames) return true;
			}
			};
		bool done = false;
		while (!done && av_read_frame(decoder.format_ctx.get(), packet.get()) >= 0) {
			if (packet->stream_index == decoder.video_stream_index) {
				ret = avcodec_send_packet(decoder.codec_ctx.get(), packet.get());
				if (ret < 0) throw_av_error("Cannot send packet to decoder codec", ret);
				done = drain_decoder();
			}
			av_packet_unref(packet.get());
		}
		if (!done) {
			avcodec_send_packet(decoder.codec_ctx.get(), nullptr);
			drain_decoder();
		}
	}
	catch (const std::exception& e) {
		std::fprintf(stderr, "Error occurred: %s\n", e.what());
	}
}