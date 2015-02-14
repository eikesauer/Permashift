/*
 * Part of permashift, a plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */


#include "overwritingringbuffer.h"


cOverwritingRingBuffer::cOverwritingRingBuffer(uint64_t bufferSize) :
m_buffer(NULL), m_bufferLength(bufferSize), m_dataStart(0), m_dataLength(0), m_dataWritten(0)
{
	if (bufferSize > 0)
	{
		Allocate(bufferSize);
	}
}

cOverwritingRingBuffer::~cOverwritingRingBuffer()
{
	free(m_buffer);
}

bool cOverwritingRingBuffer::Allocate(uint64_t bufferSize)
{
	m_bufferLength = bufferSize;

	m_buffer = (uchar*)malloc(m_bufferLength);
	if (m_buffer == NULL)
	{
		m_bufferLength = 0;
	}

	return m_buffer != NULL;
}

void cOverwritingRingBuffer::WriteData(uchar* Data, uint64_t Length)
{
	if (Length > m_bufferLength) return;

	uint64_t previousDataLength = m_dataLength;
	uint64_t dataEnd = (m_dataStart + m_dataLength) % m_bufferLength;
	if (dataEnd + Length <= m_bufferLength)
	{
		// fits without wrap-around
		memcpy(m_buffer + dataEnd, Data, Length);
	}
	else
	{
		// write with wrap-around
		uint64_t bytesTillEnd = m_bufferLength - dataEnd;
		memcpy(m_buffer + dataEnd, Data, bytesTillEnd);
		memcpy(m_buffer, Data + bytesTillEnd, Length - bytesTillEnd);
	}

	if (m_dataLength + Length <= m_bufferLength)
	{
		m_dataLength += Length;
	}
	else
	{
		uint64_t freeSpaceFilled = m_bufferLength - m_dataLength;
		uint64_t overwrittenBytes = Length - freeSpaceFilled;
		m_dataStart = (m_dataStart + overwrittenBytes) % m_bufferLength;
		m_dataLength = m_bufferLength;
	}
	m_dataWritten += Length;

	// debug buffer state
	if (m_dataLength / (100ull * 1024 * 1024) >  previousDataLength / (100ull * 1024 * 1024))
	{
		dsyslog("permashift: %d MB live video data in buffer \n", 100 * (uint)(m_dataLength / (100ull * 1024 * 1024)));
	}
}

uint64_t cOverwritingRingBuffer::ReadData(uchar** Data, uint64_t MaxLength)
{
	*Data = m_buffer + m_dataStart;
	uint64_t bytesReturned = 0;
	uint64_t bytesTillEnd = min(m_dataLength, m_bufferLength - m_dataStart);
	if (bytesTillEnd < MaxLength)
	{
		bytesReturned = bytesTillEnd;
	}
	else
	{
		bytesReturned = MaxLength;
	}
	m_dataStart = (m_dataStart + bytesReturned) % m_bufferLength;
	m_dataLength -= bytesReturned;
	return bytesReturned;
}

uint64_t cOverwritingRingBuffer::ReadDataFromEnd(uchar** Data, uint64_t MaxLength)
{
	uint64_t bytesReturned = 0;
	if (m_dataStart + m_dataLength > m_bufferLength)
	{
		uint64_t bytesFromStart = m_dataLength - (m_bufferLength - m_dataStart);
		bytesReturned = min(MaxLength, bytesFromStart);
		*Data = m_buffer + (m_dataStart + m_dataLength) % m_bufferLength - bytesReturned;
	}
	else
	{
		bytesReturned = min(MaxLength, m_dataLength);
		*Data = m_buffer + m_dataStart + m_dataLength - bytesReturned;
	}
	m_dataLength -= bytesReturned;
	*Data = m_buffer + (m_dataStart + m_dataLength) % m_bufferLength;
	return bytesReturned;
}

void cOverwritingRingBuffer::DropData(uint64_t bytesToDrop)
{
	if (bytesToDrop < m_dataLength)
	{
		uint64_t bytesTillEnd = min(m_dataLength, m_bufferLength - m_dataStart);
		m_dataLength -= bytesToDrop;
		if (bytesTillEnd > bytesToDrop)
		{
			// data to skip lies before end of buffer
			m_dataStart += bytesToDrop;
		}
		else
		{
			// data to skip wraps around
			m_dataStart = bytesToDrop - bytesTillEnd;
		}
	}
	else
	{
		// drop all
		m_dataStart = 0;
		m_dataLength = 0;
	}
}
