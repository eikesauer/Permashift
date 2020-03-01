/*
 * cBufferWriter.h
 *
 *  Created on: 05.04.2014
 *      Author: eike
 */

#ifndef CBUFFERWRITER_H_
#define CBUFFERWRITER_H_

#include <vdr/tools.h>

class cOverwritingRingBuffer;
class tFrameInfo;

/// write ring buffer contents to disc, all at once or step by step
class cBufferWriter
{
private:

	/// data source
	cOverwritingRingBuffer* m_ringBuffer;

	/// frame index of source data
	cList<tFrameInfo>* m_frameIndex;

	/// number of files to be written
	unsigned int m_fileCount;
	/// file name to use
	char* m_fileName;
	/// pointer to file number in file name
	char* m_fileNumber;

	/// file currently written
	FILE* m_currentFile;

	/// next chunk of data goes to a new file
	bool m_firstChunkInFile;

	/// byte offset of last chunk written in current file
	uint64_t m_currentFileOffset;

	/// buffer offset of very first frame in index
	uint64_t m_firstFrameOffset;

	/// total numbers of bytes to save
	uint64_t m_bytesToSaveTotal;

	/// bytes saved until now
	uint64_t m_bytesSaved;

public:

	/// Constructor.
	/// Precalculates number of files needed (although the data will be complete only later on),
	/// and reserves these files by writing dummy stuff to them.
	cBufferWriter(cOverwritingRingBuffer* ringBuffer, cList<tFrameInfo>* memoryIndex, const char* fileName, bool multipleChunks);

	virtual ~cBufferWriter();

	/// To be called when data buffer and memory index are ready for writing and will not receive additional data.
	/// Dumps some memory indices and buffer data. Assigns indices to file numbers.
	bool Initialize();

	/// Save a complete file
	void SaveFile();

	/// Save one chunk of a file
	void SaveChunk();

	/// Save all data at once
	void SaveAll();

	/// Is all saving done?
	bool Finished();

private:

	/// Prepare saving to a new file
	void StartNewFile();

};

#endif /* CBUFFERWRITER_H_ */
