#ifndef UnitigKmerCorrector_h
#define UnitigKmerCorrector_h

#include <vector>
#include <string>
#include <tuple>
#include <map>
#include "UnitigStorage.h"
#include "ReadHelper.h"

class UnitigKmerCorrector
{
	class Read
	{
	public:
		std::string name;
		std::vector<std::pair<size_t, bool>> unitigPath;
		size_t leftClip;
		size_t rightClip;
		std::string leftHanger;
		std::string rightHanger;
	};
public:
	UnitigKmerCorrector(size_t k);
	void build(const ReadpartIterator& iterator);
	template <typename F>
	void iterateRawSequences(F callback) const
	{
		for (size_t i = 0; i < reads.size(); i++)
		{
			std::string corrected = getRawSequence(i);
			callback(i, reads[i].name, corrected);
		}
	}
	std::string getCorrectedSequence(size_t readIndex, const std::vector<size_t>& context, size_t minAmbiguousCoverage, size_t minSafeCoverage) const;
	std::string getRawSequence(size_t index) const;
	size_t numReads() const;
	const std::string& getName(size_t index) const;
private:
	void getAnchorSpanners(phmap::flat_hash_map<std::pair<std::pair<size_t, bool>, std::pair<size_t, bool>>, std::map<std::vector<std::pair<size_t, bool>>, size_t>>& anchorSpannerCounts, const phmap::flat_hash_set<std::pair<std::pair<size_t, bool>, std::pair<size_t, bool>>>& adjacentAnchors, const phmap::flat_hash_set<std::pair<size_t, bool>>& isAnchor, size_t read) const;
	size_t kmerSize;
	UnitigStorage unitigs;
	std::vector<Read> reads;
};

#endif
