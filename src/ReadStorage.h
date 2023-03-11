#ifndef ReadStorage_h
#define ReadStorage_h

#include <string>
#include <vector>
#include <mutex>
#include "ReadHelper.h"
#include "fastqloader.h"

class ReadStorage
{
public:
	void storeReadsFromFile(const std::string& filename, bool includeSequences);
	template <typename F>
	void iterateReadsFromFile(const std::string& filename, size_t numThreads, bool store, F callback)
	{
		std::mutex nameMutex;
		std::vector<std::string> files { filename };
		iterateReadsMultithreaded(files, numThreads, [this, store, &nameMutex, callback](const ReadInfo& info, const std::string& sequence)
		{
			size_t name = 0;
			{
				std::lock_guard<std::mutex> guard { nameMutex };
				name = names.size();
				names.push_back(info.readName.first);
				rawReadLengths.push_back(sequence.size());
				if (store) sequences.push_back(sequence);
			}
			callback(name, sequence);
		});
	}
	template <typename F>
	void iterateReadsFromStorage(F callback) const
	{
		assert(names.size() == sequences.size());
		for (size_t i = 0; i < names.size(); i++)
		{
			callback(i, sequences[i]);
		}
	}
	std::pair<std::string, std::string> getRead(size_t i) const;
	const std::vector<std::string>& getNames() const;
	const std::vector<size_t>& getRawReadLengths() const;
	size_t size() const;
private:
	std::vector<std::string> names;
	std::vector<size_t> rawReadLengths;
	std::vector<std::string> sequences;
};

#endif
