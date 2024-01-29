#include <fstream>
#include <filesystem>
#include <mutex>
#include <iostream>
#include <vector>
#include <string>
#include <cxxopts.hpp>
#include "ReadStorage.h"
#include "MatchIndex.h"

int main(int argc, char** argv)
{
	cxxopts::Options options("matchchains_index", "Build an index file for read matching");
	options.add_options()
		("t", "Number of threads", cxxopts::value<size_t>()->default_value("1"))
		("k", "k-mer size", cxxopts::value<size_t>()->default_value("201"))
		("w", "window size", cxxopts::value<size_t>()->default_value("500"))
		("n", "window count", cxxopts::value<size_t>()->default_value("4"))
		("max-coverage", "discard indexed items with coverage higher than this", cxxopts::value<size_t>())
		("tmp-file-count", "count of temporary files used in building the index", cxxopts::value<size_t>()->default_value("16"))
		("o,output", "prefix of output index", cxxopts::value<std::string>())
		("hpc", "homopolymer compress reads before indexing", cxxopts::value<bool>()->default_value("false"))
		("keep-sequence-name-tags", "Keep tags in input sequence names")
		("h,help", "print help");
	auto result = options.parse(argc, argv);
	if (result.count("h") == 1)
	{
		std::cerr << options.help();
		std::cerr << "Usage: matchchains_index -o indexprefix readfile1.fa readfile2.fa" << std::endl;
		std::exit(0);
	}
	if (result.count("o") == 0)
	{
		std::cerr << "Output prefix -o is required" << std::endl;
		std::exit(1);
	}
	size_t numThreads = result["t"].as<size_t>();
	size_t k = result["k"].as<size_t>();
	size_t numWindows = result["n"].as<size_t>();
	size_t windowSize = result["w"].as<size_t>();
	size_t numHashPasses = result["tmp-file-count"].as<size_t>();
	size_t maxCoverage = std::numeric_limits<size_t>::max();
	if (result.count("max-coverage") == 1) maxCoverage = result["max-coverage"].as<size_t>();
	bool hpc = result["hpc"].as<bool>();
	std::string indexPrefix = result["o"].as<std::string>();
	bool keepSequenceNameTags = false;
	if (result.count("keep-sequence-name-tags")) keepSequenceNameTags = true;
	std::vector<std::string> readFiles = result.unmatched();
	if (readFiles.size() == 0)
	{
		std::cerr << "At least one input read file is required" << std::endl;
		std::exit(1);
	}
	std::cerr << "indexing with k=" << k << " n=" << numWindows << " w=" << windowSize << " hpc=" << (hpc ? 1 : 0) << " maxcoverage=" << maxCoverage << std::endl;
	std::cerr << "other parameters t=" << numThreads << " tmp-file-count=" << numHashPasses << " o=" << indexPrefix << std::endl;
	std::cerr << "indexing from files:";
	for (auto file : readFiles) std::cerr << " " << file;
	std::cerr << std::endl;
	size_t numReads = 0;
	size_t numHashes = 0;
	size_t numIndexedHashes = 0;
	std::ofstream tmpPositionsFile { indexPrefix + ".tmp", std::ofstream::binary };
	std::vector<std::ofstream> tmpHashesFiles;
	for (size_t i = 0; i < numHashPasses; i++)
	{
		tmpHashesFiles.emplace_back(indexPrefix + ".tmp" + std::to_string(i), std::ofstream::binary);
	}
	MatchIndex matchIndex { k, numWindows, windowSize };
	ReadStorage storage;
	std::vector<size_t> readLengths;
	for (auto file : readFiles)
	{
		std::mutex indexMutex;
		storage.iterateReadsFromFile(file, numThreads, false, [&matchIndex, &indexMutex, &tmpPositionsFile, &tmpHashesFiles, numHashPasses, &numReads, &numHashes, &numIndexedHashes, hpc, &readLengths](size_t readName, const std::string& sequence)
		{
			std::vector<std::tuple<uint64_t, uint32_t, uint32_t>> hashes;
			phmap::flat_hash_set<uint64_t> hashesHere;
			matchIndex.iterateWindowChunksFromRead(sequence, hpc, [&tmpPositionsFile, &tmpHashesFiles, numHashPasses, &numReads, &numHashes, &numIndexedHashes, &hashesHere, &hashes](uint32_t startPos, uint32_t endPos, uint64_t hash)
			{
				hashesHere.emplace(hash);
				hashes.emplace_back(hash, startPos, endPos);
			});
			if (hashes.size() == 0) return;
			std::lock_guard<std::mutex> lock { indexMutex };
			size_t readLength = sequence.size();
			if (hpc)
			{
				readLength = 1;
				for (size_t i = 1; i < sequence.size(); i++)
				{
					if (sequence[i] != sequence[i-1]) readLength += 1;
				}
			}
			while (readLengths.size() <= readName) readLengths.emplace_back();
			readLengths[readName] = readLength;
			numReads += 1;
			numHashes += hashes.size();
			uint32_t read = readName;
			uint32_t count = hashes.size();
			tmpPositionsFile.write((char*)&read, 4);
			tmpPositionsFile.write((char*)&count, 4);
			for (auto t : hashes)
			{
				tmpHashesFiles[std::get<0>(t) % numHashPasses].write((char*)&std::get<0>(t), 8);
				tmpPositionsFile.write((char*)&std::get<0>(t), 8);
				tmpPositionsFile.write((char*)&std::get<1>(t), 4);
				tmpPositionsFile.write((char*)&std::get<2>(t), 4);
			}
		});
	}
	const std::vector<std::string>& readNames = storage.getNames();
	tmpPositionsFile.close();
	for (size_t i = 0; i < tmpHashesFiles.size(); i++) tmpHashesFiles[i].close();
	std::cerr << numReads << " reads" << std::endl;
	std::cerr << numHashes << " total positions" << std::endl;
	phmap::flat_hash_map<uint64_t, uint32_t> hashToIndex;
	std::vector<size_t> indexCoverage;
	size_t totalDistinctHashes = 0;
	for (size_t i = 0; i < numHashPasses; i++)
	{
		std::ifstream hashPass { indexPrefix + ".tmp" + std::to_string(i), std::ios::binary };
		phmap::flat_hash_set<uint64_t> seenOnce;
		while (hashPass.good())
		{
			uint64_t hash;
			hashPass.read((char*)&hash, 8);
			if (!hashPass.good()) break;
			assert(hash % numHashPasses == i);
			if (seenOnce.count(hash) == 1)
			{
				if (hashToIndex.count(hash) == 0)
				{
					size_t index = hashToIndex.size();
					hashToIndex[hash] = index;
					indexCoverage.emplace_back(2);
				}
				else
				{
					size_t index = hashToIndex.at(hash);
					indexCoverage[index] += 1;
				}
			}
			else
			{
				seenOnce.insert(hash);
			}
		}
		totalDistinctHashes += seenOnce.size();
	}
	for (size_t i = 0; i < numHashPasses; i++)
	{
		std::filesystem::remove(indexPrefix + ".tmp" + std::to_string(i));
	}
	size_t countAboveThreshold = 0;
	size_t maxHashCoverage = 0;
	size_t maxIndexedCoverage = 0;
	for (size_t i = 0; i < indexCoverage.size(); i++)
	{
		if (indexCoverage[i] > maxCoverage)
		{
			countAboveThreshold += 1;
		}
		else
		{
			maxIndexedCoverage = std::max(maxIndexedCoverage, indexCoverage[i]);
		}
		maxHashCoverage = std::max(maxHashCoverage, indexCoverage[i]);
	}
	std::cerr << countAboveThreshold << " hashes discarded due to being above max coverage" << std::endl;
	std::cerr << maxHashCoverage << " max found coverage" << std::endl;
	std::cerr << maxIndexedCoverage << " max indexed coverage" << std::endl;
	std::cerr << totalDistinctHashes << " distinct hashes" << std::endl;
	std::cerr << hashToIndex.size() << " indexed hashes" << std::endl;
	{
		std::ofstream indexMetadata { indexPrefix + ".metadata", std::ofstream::binary };
		unsigned char ishpc = (hpc ? 1 : 0);
		indexMetadata.write((char*)&ishpc, 1);
		indexMetadata.write((char*)&k, sizeof(size_t));
		indexMetadata.write((char*)&numWindows, sizeof(size_t));
		indexMetadata.write((char*)&windowSize, sizeof(size_t));
		indexMetadata.write((char*)&maxCoverage, sizeof(size_t));
		size_t countHashes = hashToIndex.size();
		size_t countReads = readLengths.size();
		indexMetadata.write((char*)&countHashes, sizeof(size_t));
		indexMetadata.write((char*)&countReads, sizeof(size_t));
		for (size_t read = 0; read < countReads; read++)
		{
			std::string name = readNames[read];
			if (!keepSequenceNameTags) name = name.substr(0, name.find_first_of(" \t\r\n"));
			size_t nameLength = name.size();
			size_t readLength = readLengths[read];
			indexMetadata.write((char*)&readLength, sizeof(size_t));
			indexMetadata.write((char*)&nameLength, sizeof(size_t));
			indexMetadata.write((char*)name.data(), nameLength);
		}
	}
	std::ofstream indexFile { indexPrefix + ".positions", std::ofstream::binary };
	std::ifstream redoPositions { indexPrefix + ".tmp", std::ifstream::binary };
	while (redoPositions.good())
	{
		uint32_t read;
		uint32_t count;
		redoPositions.read((char*)&read, 4);
		redoPositions.read((char*)&count, 4);
		if (!redoPositions.good()) break;
		std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> hashes;
		for (size_t i = 0; i < count; i++)
		{
			uint64_t hash;
			uint32_t startPos, endPos;
			redoPositions.read((char*)&hash, 8);
			redoPositions.read((char*)&startPos, 4);
			redoPositions.read((char*)&endPos, 4);
			if (hashToIndex.count(hash) == 0) continue;
			size_t index = hashToIndex.at(hash);
			if (indexCoverage[index] > maxCoverage) continue;
			hashes.emplace_back(index, startPos, endPos);
		}
		if (hashes.size() == 0) continue;
		count = hashes.size();
		indexFile.write((char*)&read, 4);
		indexFile.write((char*)&count, 4);
		numIndexedHashes += count;
		for (auto t : hashes)
		{
			indexFile.write((char*)&std::get<0>(t), 4);
			indexFile.write((char*)&std::get<1>(t), 4);
			indexFile.write((char*)&std::get<2>(t), 4);
		}
	}
	redoPositions.close();
	std::filesystem::remove(indexPrefix + ".tmp");
	std::cerr << numIndexedHashes << " indexed positions" << std::endl;
}