#ifndef EXPORT_FRAMES_H
#define EXPORT_FRAMES_H

#include "VideoUtils.h"
#include <string>

void export_png_frames(const DecoderState& decoder, const std::string& output_dir, int max_frames);
void export_ppm_frames(const DecoderState& decoder, const std::string& output_dir, int max_frames);

#endif // EXPORT_FRAMES_H