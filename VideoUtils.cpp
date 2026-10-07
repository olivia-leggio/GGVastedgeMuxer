#include "VideoUtils.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

[[noreturn]] void throw_av_error(const std::string& what, int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = { 0 };
    av_strerror(err, buf, sizeof(buf));
    throw std::runtime_error(what + ": " + buf);
}

DecoderState open_input_file(const std::string& path) {
    DecoderState decoder{};

    AVFormatContext* raw_format_ctx{ nullptr };
    int ret = avformat_open_input(&raw_format_ctx, path.c_str(), nullptr, nullptr);
    if (ret < 0) { throw_av_error("Failed to open '" + path + "'", ret); }
    decoder.format_ctx.reset(raw_format_ctx);

    ret = avformat_find_stream_info(decoder.format_ctx.get(), nullptr);
    if (ret < 0) { throw_av_error("Cannot find stream information", ret); }

    const AVCodec* codec{ nullptr };
    ret = av_find_best_stream(decoder.format_ctx.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (ret < 0) { throw std::runtime_error("Cannot find a video stream in the input file"); }
    decoder.video_stream_index = ret;

    decoder.codec_ctx.reset(avcodec_alloc_context3(codec));
    if (!decoder.codec_ctx.get()) { throw std::runtime_error("Cannot allocate decoder context for video stream"); }

    ret = avcodec_parameters_to_context(decoder.codec_ctx.get(), decoder.format_ctx->streams[decoder.video_stream_index]->codecpar);
    if (ret < 0) { throw_av_error("Cannot copy codec parameters to codec context", ret); }


    ret = avcodec_open2(decoder.codec_ctx.get(), codec, nullptr);
    if (ret < 0) { throw std::runtime_error("Cannot open video decodex"); }

    return decoder;
}

void print_file_info(const DecoderState& decoder) {
    std::cout << "Container info:\n";
    std::cout << "  Format:      " << decoder.format_ctx->iformat->name << " (" << decoder.format_ctx->iformat->long_name << ")\n";

    if (decoder.format_ctx->duration != AV_NOPTS_VALUE) {
        double duration_sec = decoder.format_ctx->duration / static_cast<double>(AV_TIME_BASE);
        std::cout << "  Duration:    " << duration_sec << " s\n";
    }
    else {
        std::cout << "  Duration:    unknown\n";
    }
    if (decoder.format_ctx->bit_rate > 0) {
        std::cout << "  Bit rate:    " << decoder.format_ctx->bit_rate / 1000 << " kb/s\n";
    }

    std::cout << "  Streams:     " << decoder.format_ctx->nb_streams << "\n";
    for (unsigned int i = 0; i < decoder.format_ctx->nb_streams; ++i) {
        std::cout << "  Stream #" << i << ": ";

        AVStream* stream = decoder.format_ctx->streams[i];
        AVCodecParameters* params = stream->codecpar;

        const char* codec_name = avcodec_get_name(params->codec_id);

        switch (params->codec_type) {
        case AVMEDIA_TYPE_VIDEO: {
            AVRational frame_rate = av_guess_frame_rate(decoder.format_ctx.get(), stream, nullptr);
            double fps = frame_rate.den ? av_q2d(frame_rate) : 0.0;

            std::cout << "video, codec=" << codec_name
                << ", " << params->width << "x" << params->height
                << ", " << fps << " fps"
                << ", pixel format=" << av_get_pix_fmt_name(static_cast<AVPixelFormat>(params->format))
                << "\n";
            break;
        }
        case AVMEDIA_TYPE_AUDIO: {
            std::cout << "audio, codec=" << codec_name
                << ", " << params->sample_rate << " Hz"
                << ", " << params->ch_layout.nb_channels << " channel(s)"
                << ", sample format=" << av_get_sample_fmt_name(static_cast<AVSampleFormat>(params->format))
                << "\n";
            break;
        }
        default: {
            std::cout << "other (" << av_get_media_type_string(params->codec_type) << ")\n";
            break;
        }
        }
    }
    std::cout.flush();
}

void print_frames_info(const DecoderState& decoder, int max_frames) {
    std::cout << "Frame-by-frame info (first " << max_frames << " frames):\n";
    std::cout << "  idx  type  key  pts        pts_time(s)  width  height  pix_fmt\n";
    std::cout << "  ---  ----  ---  ---------  -----------  -----  ------  -------\n";

    std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
    std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
    int printed = 0;

    // Pulls every frame currently buffered in the decoder and prints it.
    // Returns false if we've hit our print limit and should stop entirely.
    auto drain_and_print = [&]() -> bool {
        while (true) {
            int ret = avcodec_receive_frame(decoder.codec_ctx.get(), frame.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return true; // need more input, or fully done
            if (ret < 0) throw_av_error("Error during decoding", ret);

            int64_t pts = frame->best_effort_timestamp;
            double pts_time = (pts != AV_NOPTS_VALUE) ? pts * av_q2d(decoder.format_ctx->streams[decoder.video_stream_index]->time_base) : -1.0;

            bool is_key_frame = (frame->flags & AV_FRAME_FLAG_KEY) != 0;

            std::printf("  %-4d %-5c %-4d %-10lld %-12.3f %-6d %-7d %s\n",
                printed,
                av_get_picture_type_char(frame->pict_type), // I, B, P
                is_key_frame,
                static_cast<long long>(pts),
                pts_time,
                frame->width,
                frame->height,
                av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format)));

            av_frame_unref(frame.get());

            ++printed;
            if (printed >= max_frames) return false; // hit the limit, stop the outer loop
        }
        };

    bool keep_going = true;
    int ret{};
    // stops if max_frames is reached, or if av_read_frame doesn't return a valid frame (end of file)
    while (keep_going && av_read_frame(decoder.format_ctx.get(), packet.get()) >= 0) {
        if (packet->stream_index == decoder.video_stream_index) {
            ret = avcodec_send_packet(decoder.codec_ctx.get(), packet.get());
            if (ret < 0 && ret != AVERROR(EAGAIN)) throw_av_error("Error sending packet", ret);
            keep_going = drain_and_print();
        }
        av_packet_unref(packet.get());
    }

    // nullptr to avcodec_send_packet tells the codec to flush because the end of the stream is reached
    if (keep_going) {
        avcodec_send_packet(decoder.codec_ctx.get(), nullptr);
        drain_and_print();
    }

    std::cout << "\n";
}

void print_frame_ascii(const AVFrame* frame, AVRational time_base, bool clear_screen) {
    int x{};
    int y{};
    uint8_t* p0;
    uint8_t* p;
    int64_t delay{};
    int64_t last_pts = AV_NOPTS_VALUE;

    if (frame->pts != AV_NOPTS_VALUE) {
        if (last_pts != AV_NOPTS_VALUE) {
            delay = av_rescale_q(frame->pts - last_pts,
                time_base, AV_TIME_BASE_Q);
            // try to match actual framerate of the video
            if (delay > 0 && delay < 1000000) {
                std::this_thread::sleep_for(std::chrono::microseconds(delay));
            }
        }
        last_pts = frame->pts;
    }
    // ASCII grayscale display
    p0 = frame->data[0];
    if (clear_screen) std::cout << "\033c";
    for (y = 0; y < frame->height; y++) {
        p = p0;
        for (x = 0; x < frame->width; x++) {
            std::putchar(" .-+#"[*(p++) / 52]);
        }
        std::putchar('\n');
        p0 += frame->linesize[0];
    }
    if (!clear_screen) std::cout << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
    std::cout.flush();
}

void save_frame_to_png(FrameTransferState& fts, std::string& path) {
    if (!fts.in_frame) throw std::runtime_error("Input frame to save to png is not defined");
    fts.out_frame.reset(av_frame_alloc());
    fts.out_frame->format = AV_PIX_FMT_RGB24;
    fts.out_frame->width = fts.in_frame->width;
    fts.out_frame->height = fts.in_frame->height;

    // reallocate PNG context if it doesn't match the output parameters
    if (!fts.codec_ctx ||
        fts.codec_ctx->width != fts.out_frame->width ||
        fts.codec_ctx->height != fts.out_frame->height ||
        fts.codec_ctx->pix_fmt != (AVPixelFormat)fts.out_frame->format) {

        std::cout << "Reallocating PNG codec context\n";

        const AVCodec* png_codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
        if (!png_codec) throw std::runtime_error("PNG encoder not available in this FFmpeg build");

        std::unique_ptr<AVCodecContext, CodecCtxDeleter> temp_ctx(avcodec_alloc_context3(png_codec));
        if (!temp_ctx) throw std::runtime_error("Failed to allocate PNG encoder context");

        temp_ctx->width = fts.out_frame->width;
        temp_ctx->height = fts.out_frame->height;
        temp_ctx->pix_fmt = AV_PIX_FMT_RGB24;
        temp_ctx->time_base = AVRational{ 1, 25 }; // needs to be set to init, but it isn't used

        int ret = avcodec_open2(temp_ctx.get(), png_codec, nullptr);
        if (ret < 0) throw_av_error("Failed to open PNG encoder", ret);
        fts.codec_ctx = std::move(temp_ctx);
    }

    // reallocate the rescaling context if it doesn't match the in and out parameters
    SwsContext* temp_ctx = fts.sws_ctx.release();
    temp_ctx = sws_getCachedContext(temp_ctx, fts.in_frame->width, fts.in_frame->height, (AVPixelFormat)fts.in_frame->format, fts.out_frame->width, fts.out_frame->height, (AVPixelFormat)fts.out_frame->format, SWS_BILINEAR, nullptr, nullptr, nullptr);
    fts.sws_ctx.reset(temp_ctx);
    if (!fts.sws_ctx) throw std::runtime_error("Failed to create SwsContext");

    int ret = av_frame_get_buffer(fts.out_frame.get(), 0);
    if (ret < 0) throw_av_error("Failed to allocate RGB frame buffer", ret);

    // convert pixels to rgb
    ret = sws_scale_frame(fts.sws_ctx.get(), fts.out_frame.get(), fts.in_frame.get());
    if (ret < 0) throw_av_error("Failed to convert frame to RGB", ret);

    ret = avcodec_send_frame(fts.codec_ctx.get(), fts.out_frame.get());
    if (ret < 0) throw_av_error("Failed to send frame to PNG encoder", ret);
    av_frame_unref(fts.out_frame.get());

    std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
    ret = avcodec_receive_packet(fts.codec_ctx.get(), packet.get());
    if (ret < 0) throw_av_error("Failed to receive packet from PNG encoder", ret);

    // write to file
    std::ofstream out_file(path, std::ios::out | std::ios::binary);
    out_file.write(reinterpret_cast<const char*>(packet->data), packet->size);
    out_file.close();
}

void save_frame_to_ppm(FrameTransferState& fts, std::string& path) {
    if (!fts.in_frame) throw std::runtime_error("Input frame to save to ppm is not defined");
    fts.out_frame.reset(av_frame_alloc());
    fts.out_frame->format = AV_PIX_FMT_RGB24;
    fts.out_frame->width = fts.in_frame->width;
    fts.out_frame->height = fts.in_frame->height;

    // reallocate the rescaling context if it doesn't match the in and out parameters
    SwsContext* temp_ctx = fts.sws_ctx.release();
    temp_ctx = sws_getCachedContext(temp_ctx, fts.in_frame->width, fts.in_frame->height, (AVPixelFormat)fts.in_frame->format, fts.out_frame->width, fts.out_frame->height, (AVPixelFormat)fts.out_frame->format, SWS_BILINEAR, nullptr, nullptr, nullptr);
    fts.sws_ctx.reset(temp_ctx);
    if (!fts.sws_ctx) throw std::runtime_error("Failed to create SwsContext");

    int ret = av_frame_get_buffer(fts.out_frame.get(), 0);
    if (ret < 0) throw_av_error("Failed to allocate RGB frame buffer", ret);

    // convert pixels to rgb
    ret = sws_scale_frame(fts.sws_ctx.get(), fts.out_frame.get(), fts.in_frame.get());
    if (ret < 0) throw_av_error("Failed to convert frame to RGB", ret);

    // write to file
    if (*(fts.out_frame->linesize) < 0) throw std::runtime_error("Linesize is negative for image encoding");
    int size_to_write = *(fts.out_frame->linesize) * fts.out_frame->height;

    std::ofstream out_file(path, std::ios::out | std::ios::binary);
    out_file << "P6\n" << fts.out_frame->width << " " << fts.out_frame->height << "\n" << "255\n";
    out_file.write(reinterpret_cast<const char*>(fts.out_frame->data[0]), size_to_write);
    out_file.close();
}