#include "MkvWriter.h"
#include "VideoUtils.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

// writes video frames without audio
// this is meant purely for my test case, so it does not handle any edge cases
int ppms_to_mkv(const std::string& inputDir, const std::string& outputFilePath) {
	int processedFrameCount{ 0 };

	// get list of files
	if (!std::filesystem::is_directory(inputDir)) throw std::runtime_error("Input directory does not exist: " + inputDir);
	std::vector<std::string> ppmFiles;
	for (const auto& entry : std::filesystem::directory_iterator(inputDir)) {
		if (entry.is_regular_file() && entry.path().extension() == ".ppm") {
			ppmFiles.push_back(entry.path().string());
		}
	}
	if (ppmFiles.empty()) return -1;
	std::sort(ppmFiles.begin(), ppmFiles.end());

	// read first file to get dimensions
	int width{ 576 };
	int height{ 576 };
	/*
	std::string& firstFilePath = ppmFiles.front();
	std::ifstream firstFile(firstFilePath);
	if (firstFile.is_open()) {
		std::string line;
		std::getline(firstFile, line);
		if (line != "P6") {
			firstFile.close();
			throw std::runtime_error("Invalid PPM file format in file: " + firstFilePath);
		}
		std::getline(firstFile, line);
		std::istringstream dimensions(line);
		dimensions >> width >> height;
	}
	else {
		throw std::runtime_error("Failed to open first PPM file: " + firstFilePath);
	}
	*/


	// read through all files and write to MKV as frames
	MkvWriter writer(outputFilePath, width, height, 25, true);

	int64_t pts{ 0 };
	int expectedPixelDataSize = width * height * 3;
	auto pixelBuffer = std::make_unique_for_overwrite<char[]>(expectedPixelDataSize);
	for (const auto& ppmFilePath : ppmFiles) {
		try {
			std::streampos pixelDataStartPos{ 0 };
			unsigned int pixelDataSize{ 0 };

			std::ifstream ppmFile(ppmFilePath, std::ios::binary);
			if (!ppmFile.is_open()) {
				throw std::runtime_error("Failed to open PPM file: " + ppmFilePath);
			}
			// skip header to get to pixel data
			for (int i = 0; i < 3; ++i) {
				ppmFile.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
			}
			pixelDataStartPos = ppmFile.tellg();
			ppmFile.seekg(0, std::ios::end);
			pixelDataSize = ppmFile.tellg() - pixelDataStartPos;
			if (pixelDataSize != expectedPixelDataSize) {
				ppmFile.close();
				throw std::runtime_error("Unexpected pixel data size in file: " + ppmFilePath + " (expected " + std::to_string(expectedPixelDataSize) + " received " + std::to_string(pixelDataSize) + ")");
			}
			ppmFile.clear();
			ppmFile.seekg(pixelDataStartPos, std::ios::beg);
			ppmFile.read(pixelBuffer.get(), pixelDataSize);
			ppmFile.close();

			std::cout << "Successfully read PPM file: " << ppmFilePath << "(" << pixelDataSize << " bytes)" << "\n";

			processedFrameCount = processedFrameCount + writer.writeVideoBufferFrame(pixelBuffer.get(), pixelDataSize, pts);

			++pts;
		}
		catch (const std::exception& e) {
			std::cerr << "Error processing file '" << ppmFilePath << "': " << e.what() << "\n";
			return processedFrameCount;
		}
	}
	processedFrameCount = processedFrameCount + writer.writeVideoAVFrame(nullptr, pts);
	writer.finalizeFile();
	return processedFrameCount;
}


int main(int argc, char* argv[])
{
	if (argc < 3) {
		std::cout << "Program must be provided with an output file name and the directory of input frames\n";
		std::cout << "VastedgeOutputMuxer <output_file> <input_dir>" << std::endl;
		return 1;
	}
	std::string inputDirPath{ argv[2]};
    std::string outFilePath{ argv[1]};
	try {
		int processedFrameCount = ppms_to_mkv(inputDirPath, outFilePath);
		std::cout << "Successfully processed " << processedFrameCount << " frames.\n";
	}
	catch (const std::exception& e) {
		std::cerr << "Error during PPM to MKV conversion: " << e.what() << "\n";
	}

    std::cout << "Done. Press Enter to exit..." << std::endl;
    std::cin.get();
    return 0;
}
