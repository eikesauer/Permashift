/*
 * Part of permashift, a plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */


#ifndef OVERWRITINGRINGBUFFER_H_
#define OVERWRITINGRINGBUFFER_H_

#include <vdr/tools.h>

/// ring buffer overwriting oldest data when filled
class cOverwritingRingBuffer
{
private:

	uchar* m_buffer;			///< data container
	uint64_t m_bufferLength;	///< size of buffer
	uint64_t m_dataStart;		///< offset of data start
	uint64_t m_dataLength;		///< used bytes in buffer
	uint64_t m_dataWritten;		///< total bytes written to buffer (livetime)

public:

	cOverwritingRingBuffer(uint64_t bufferSize);

	virtual ~cOverwritingRingBuffer();

	/// allocates buffer - only needed if size 0 has been given to constructor
	/// returns false if out of memory
	bool Allocate(uint64_t bufferSize);

	/// writes data to the buffer
	void WriteData(uchar* Data, uint64_t Length);

	/// fetches and removes up to maxLength bytes from the buffer
	/// the pointer provided is not to be deleted by the caller
	uint64_t ReadData(uchar** Data, uint64_t MaxLength);

	/// fetches and removes up to maxLength bytes from the end of the buffer
	/// the pointer provided is not to be deleted by the caller
	uint64_t ReadDataFromEnd(uchar** Data, uint64_t MaxLength);

	/// drops bytes from current beginning of buffer
	void DropData(uint64_t bytesToDrop);

	/// bytes available
	uint64_t BytesAvailable() { return m_dataLength; }

	/// total bytes written in buffer lifetime
	uint64_t BytesWritten() { return m_dataWritten; }

	/// total bytes dropped in buffer lifetime
	uint64_t BytesDropped() { return BytesWritten() - BytesAvailable(); }

};

#endif /* OVERWRITINGRINGBUFFER_H_ */
