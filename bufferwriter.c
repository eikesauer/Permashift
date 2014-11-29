/*
 * Part of permashift, a plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */


#define VIDEO_FILE_SIZE (50 * 1024 * 1024)
#define SAVING_HEAP_SIZE (unsigned int)(5 * 1024 * 1024)

// copied from recording.c
#define RECORDFILESUFFIXTS      "/%05d.ts"
#define RECORDFILESUFFIXLEN 20 // some additional bytes for safety...


#include "bufferwriter.h"
#include "bufferreceiver.h"


cBufferWriter::cBufferWriter(cOverwritingRingBuffer* ringBuffer, cList<tFrameInfo>* memoryIndex, const char* fileName, bool multipleChunks) :
m_ringBuffer(ringBuffer), m_frameIndex(memoryIndex), m_currentFile(NULL), m_firstChunkInFile(true), m_currentFileOffset(0)
{
	// copy target file name
	m_fileName = MALLOC(char, strlen(fileName) + RECORDFILESUFFIXLEN);
	strcpy(m_fileName, fileName);
	m_fileNumber = m_fileName + strlen(fileName);

	// calculate number of files to be used
	m_fileCount = 1;
	if (multipleChunks)
	{
		m_fileCount = max(1, (int)(m_ringBuffer->BytesAvailable() / VIDEO_FILE_SIZE));
	}

	// reserve files for our memory buffer by creating them
	for (unsigned int fileIndex = 1; fileIndex <= m_fileCount; fileIndex++)
	{
		sprintf(m_fileNumber, RECORDFILESUFFIXTS, fileIndex);
		FILE* tempFile = fopen(m_fileName, "wb");
		if (!tempFile)
		{
			esyslog("Permashift: Could not open file '%s'!", m_fileName);
			// give up
			m_fileCount = 0;
		}
		// ... and writing stuff to it (otherwise it will be deleted by the receiver).
		char dummyText[] = "permashift dummy recording file - to be filled with video later";
		fwrite(dummyText, sizeof(dummyText), 1, tempFile);
		fclose(tempFile);
	}
}

cBufferWriter::~cBufferWriter()
{
	if (m_fileName != NULL)
	{
		free(m_fileName);
		m_fileName = NULL;
	}
}

bool cBufferWriter::Finished()
{
	return m_fileCount == 0;
}

bool cBufferWriter::Initialize()
{
	// delete indices up to first I frame
	tFrameInfo* frameInfo = m_frameIndex->First();
	while (frameInfo != NULL && !frameInfo->iFrame)
	{
		m_frameIndex->Del(frameInfo);
		frameInfo = m_frameIndex->First();
	}
	if (frameInfo == NULL)
	{
		return false;
	}

	// throw away video data up to first I frame
	m_firstFrameOffset = frameInfo->offset;
	m_ringBuffer->DropData(m_firstFrameOffset - m_ringBuffer->BytesDropped());

	// all data left after dropping has to be saved
	m_bytesToSaveTotal = m_ringBuffer->BytesAvailable();

	// assign frames to files
	if (m_fileCount > 1)
	{
		unsigned int approxFileSize = (m_frameIndex->Last()->offset - m_firstFrameOffset) / m_fileCount;
		unsigned int fileIndex = 1;
		do
		{
			frameInfo->fileNo = fileIndex;
			frameInfo = (tFrameInfo*)frameInfo->Next();
			if (frameInfo == NULL) break;
			// if we're not at the last file...
			if (fileIndex <= m_fileCount)
			{
				// check if we're at the first I frame after the approximate file size
				if (frameInfo->iFrame && frameInfo->offset - m_firstFrameOffset > (uint64_t)fileIndex * approxFileSize)
				{
					fileIndex++;
				}
			}
		}
		while (frameInfo != NULL);
	}

	m_firstChunkInFile = true;
	m_bytesSaved = 0;

	return true;
}


void cBufferWriter::SaveAll()
{
	if (m_fileName == NULL) return;

	// write video file
	uchar* data;
	uint64_t bytesRead = 0;
	FILE* outFile = fopen(m_fileName, "wb");
	while ((bytesRead = m_ringBuffer->ReadData(&data, VIDEO_FILE_SIZE)) > 0)
	{
		if (fwrite(data, bytesRead, 1, outFile) != 1)
		{
			esyslog("Error writing file %s!", m_fileName);
			break;
		}
	}
	fclose(outFile);
}


void cBufferWriter::SaveFile()
{
	if (m_fileName == NULL) return;

	do
	{
		SaveChunk();
	}
	while (!m_firstChunkInFile && !Finished());
}


void cBufferWriter::SaveChunk()
{
	if (m_fileName == NULL) return;

	if (m_firstChunkInFile)
	{
		StartNewFile();
	}

	uchar* data;
	uint64_t bytesRead = 0;
	if ((bytesRead = m_ringBuffer->ReadDataFromEnd(&data, min(m_currentFileOffset, (uint64_t)(SAVING_HEAP_SIZE)))) > 0)
	{
		m_currentFileOffset -= bytesRead;
		fseek(m_currentFile, m_currentFileOffset, SEEK_SET);
		if (fwrite(data, bytesRead, 1, m_currentFile) != 1)
		{
			esyslog("Error writing file %s!", m_fileName);
			// give up
			m_fileCount = 0;
		}
		m_bytesSaved += bytesRead;
	}

	if (m_currentFileOffset == 0)
	{
		fclose(m_currentFile);

		m_fileCount--;
		m_firstChunkInFile = true;
	}

	if (bytesRead == 0)
	{
		// give up
		m_fileCount = 0;
	}
}


void cBufferWriter::StartNewFile()
{
	// find frames for next file
	tFrameInfo* frameInfo = m_frameIndex->Last();
	while (frameInfo != NULL)
	{
		tFrameInfo* previousFrameInfo = (tFrameInfo*) frameInfo->Prev();
		if (previousFrameInfo == NULL || previousFrameInfo->fileNo < m_fileCount)
		{
			break;
		}
		frameInfo = previousFrameInfo;
	}

	// calculate file length
	m_currentFileOffset = (m_bytesToSaveTotal - m_bytesSaved) - (frameInfo->offset - m_firstFrameOffset);

	// delete frame infos for this file from list
	while (frameInfo != NULL)
	{
		tFrameInfo* nextFrameInfo = (tFrameInfo*) frameInfo->Next();
		m_frameIndex->Del(frameInfo);
		frameInfo = nextFrameInfo;
	}
	m_firstChunkInFile = false;

	// allocate video file in full size (hopefully creating a sparse file),
	// so we can seek to the end later on
	sprintf(m_fileNumber, RECORDFILESUFFIXTS, m_fileCount);
	dsyslog("writing... %s", m_fileName);
	int retVal = truncate(m_fileName, m_currentFileOffset);
	if (retVal != 0)
	{
		esyslog("Permashift: Could not resize file '%s' (%d/%d)!", m_fileName, retVal, errno);
		// give up
		m_fileCount = 0;
	}

	// open file
	m_currentFile = fopen(m_fileName, "r+b");
}
