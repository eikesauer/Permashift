/*
 * Part of permashift, a plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */


#include "bufferreceiver.h"

#include <vdr/recording.h>
#include <vdr/skins.h>
#include "permashift.h"

// copied from recording.c
#define MAXBROKENTIMEOUT 30 // seconds


cBufferReceiver::cBufferReceiver() : cRecorder(NULL, NULL, -1),
 m_channel(NULL),
 m_recordingMode(MemoryRecording),
 m_bufferWriter(NULL),
 // adding some TS packets to make sure it works with as well as without Klaus' patch to remux.c 
 m_syncBuffer(1024 * 1024, (MIN_TS_PACKETS_FOR_FRAME_DETECTOR + 5) * TS_SIZE),
 m_saveOnTheFly(false),
 m_owner(NULL)
{
	m_ringBuffer = new cOverwritingRingBuffer(0);
}

cBufferReceiver::~cBufferReceiver()
{
	// just to make sure we're not hanging anywhere
	m_syncCondition.Signal();

	// too late when it's called in cRecorder destructor, then our Activate(false) would not be called anymore...
	cReceiver::Detach();

	if (m_owner != NULL)
	{
		m_owner->BufferDeleted(this);
	}
	if (m_bufferWriter != NULL)
	{
		delete m_bufferWriter;
	}
	if (m_ringBuffer != NULL)
	{
		delete m_ringBuffer;
	}
}

bool cBufferReceiver::Allocate(uint64_t bufferSize)
{
	return m_ringBuffer->Allocate(bufferSize / 188 * 188);
}

void cBufferReceiver::SetOwner(cPluginPermashift* owner)
{
	m_owner = owner;
}

void  cBufferReceiver::SetChannel(const cChannel *newChannel)
{
	m_channel = newChannel;
	SetPids(m_channel);
}

void cBufferReceiver::SetSavingOnTheFly(bool saveOnTheFly)
{
	// only allowed to change before buffer is activated
	if (m_recordingMode == MemoryRecording)
	{
		m_saveOnTheFly = saveOnTheFly;
	}
}

bool cBufferReceiver::IsPreRecording(const cChannel *Channel)
{
	return m_recordingMode == MemoryRecording && m_channel == Channel;
}

void cBufferReceiver::Activate(bool On)
{
	if (!On)
	{
		Cancel(3);
	}
}

void cBufferReceiver::Receive(
#ifdef VDR_2_3	  
				const 
#endif
				uchar *Data, int Length)
{
	m_bufferSwitchMutex.Lock();

	// create frame detector at start of memory receiving
	if (frameDetector == NULL)
	{
		frameDetector = new cFrameDetector(m_channel->Vpid(), m_channel->Vtype());
	}

	// in recording phase, pass data to disc recorder
	if (m_recordingMode == FileRecording)
	{
		cRecorder::Receive(Data, Length);
	}

	// During memory recording and synchronization phase,
	// we write the data into our sync buffer.
	else if (m_recordingMode == MemoryRecording || m_recordingMode == SyncingPhase)
	{
		m_syncBuffer.Put(Data, Length);

		int r;
		uchar *b = m_syncBuffer.Get(r);
		if (b)
		{
			int Count = frameDetector->Analyze(b, r);
			if (Count)
			{
				bool switchToRecorder = false;
				if (frameDetector->Synced())
				{
					if (frameDetector->NewFrame())
					{
						// check switch to disc recording
						if (m_recordingMode == SyncingPhase && frameDetector->Synced() && frameDetector->IndependentFrame())
						{
							switchToRecorder = true;
						}
						else
						{
							// add new frame information to our index
							m_frameIndex.Add(new tFrameInfo(frameDetector->IndependentFrame(), m_ringBuffer->BytesWritten()), m_frameIndex.Last());
							// if (m_bufferWriter != NULL)
							// {
								// add to last file of buffer writer
								// ((tFrameInfo*)m_frameIndex.Last())->fileNo = m_bufferWriter->FileNumber();
							// }
						}
					}
					// inject PAT/PMT at new I frame
					if (frameDetector->IndependentFrame())
					{
						cPatPmtGenerator patPmtGenerator(m_channel);
						m_ringBuffer->WriteData(patPmtGenerator.GetPat(), TS_SIZE);
						int Index = 0;
						while (uchar *pmt = patPmtGenerator.GetPmt(Index))
						{
							m_ringBuffer->WriteData(pmt, TS_SIZE);
						}
					}
				}

				if (!switchToRecorder)
				{
					// data is recorded to memory
					m_ringBuffer->WriteData(b, Count);
					m_syncBuffer.Del(Count);

					// delete frame information for frames just overwritten
					tFrameInfo* firstFrameInfo = NULL;
					do
					{
						firstFrameInfo = m_frameIndex.First();
						if (firstFrameInfo == NULL || firstFrameInfo->offset >= m_ringBuffer->BytesDropped())
						{
							break;
						}
						m_frameIndex.Del(firstFrameInfo);
					} while (true);

					/*
					static bool overflowReported = false;
					if (!overflowReported && m_ringBuffer->BytesDropped() > 0)
					{
						Skins.QueueMessage(mtInfo, tr("Debug: Permashift buffer fully used"));
						overflowReported = true;
					}
					*/
				}
				else
				{
					// switch to disc recording
					dsyslog("permashift: starting file recording \n");
					m_recordingMode = FileRecording;

					// prepare memory buffer and index
					m_bufferWriter->Initialize();

					// write index file
					tFrameInfo* frameInfo = m_frameIndex.First();
					uint64_t firstByte = frameInfo->offset;
					unsigned int currentFileNo = 0;
					while (frameInfo != NULL)
					{
						if (frameInfo->fileNo > currentFileNo)
						{
							currentFileNo = frameInfo->fileNo;
							firstByte = frameInfo->offset;
						}
						index->Write(frameInfo->iFrame, frameInfo->fileNo, frameInfo->offset - firstByte);
						frameInfo = (tFrameInfo*)frameInfo->Next();
					}

					// write (parts of) buffer
					if (m_saveOnTheFly)
					{
						m_bufferWriter->SaveFile();
					}
					else
					{
						m_bufferWriter->SaveAll();
					}

					// start recorder thread
					Start();

					// flush sync buffer to recorder
					int r;
					uchar *b = NULL;
					do
					{
						b = m_syncBuffer.GetRest(r);
						if (b != NULL)
						{
							cRecorder::Receive(b, r);
							m_syncBuffer.Del(r);
						}
					} while (b != NULL && r > 0);

					// signal end of sync phase
					m_syncCondition.Signal();
				}
			}
		}
	}

	m_bufferSwitchMutex.Unlock();
}

// copied from recorder.c,
// except live buffer saving and starting with FirstIframeSeen = true
void cBufferReceiver::Action()
{
	time_t t = time(NULL);
	bool InfoWritten = false;
	bool FirstIframeSeen = true;
	int liveByteCount = 0;
	int liveBytesProcessed = 0;
	while (Running())
	{
		int r;
		uchar *b = ringBuffer->Get(r);
		if (b)
		{
			if (liveByteCount == 0)
			{
				liveByteCount = r;
			}

			int Count = frameDetector->Analyze(b, r);
			if (Count)
			{
				if (!Running() && frameDetector->IndependentFrame()) // finish the recording before the next independent frame
					break;
				if (frameDetector->Synced())
				{
					if (!InfoWritten)
					{
						cRecordingInfo RecordingInfo(recordingName);
						if (RecordingInfo.Read())
						{
							if (frameDetector->FramesPerSecond() > 0
								&& DoubleEqual(RecordingInfo.FramesPerSecond(),DEFAULTFRAMESPERSECOND)
								&& !DoubleEqual(RecordingInfo.FramesPerSecond(), frameDetector->FramesPerSecond()))
							{
								RecordingInfo.SetFramesPerSecond(
										frameDetector->FramesPerSecond());
								RecordingInfo.Write();
#ifdef VDR_2_3
                                                                LOCK_RECORDINGS_WRITE;
								Recordings->UpdateByName(recordingName);
#else
                                                                Recordings.UpdateByName(recordingName);
#endif
							}
						}
						InfoWritten = true;
					}
					if (FirstIframeSeen || frameDetector->IndependentFrame())
					{
						FirstIframeSeen = true; // start recording with the first I-frame
						if (!NextFile())
							break;
						if (index && frameDetector->NewFrame())
							index->Write(frameDetector->IndependentFrame(),
									fileName->Number(), fileSize);
						if (frameDetector->IndependentFrame())
						{
							recordFile->Write(patPmtGenerator.GetPat(),
									TS_SIZE);
							fileSize += TS_SIZE;
							int Index = 0;
							while (uchar *pmt = patPmtGenerator.GetPmt(Index))
							{
								recordFile->Write(pmt, TS_SIZE);
								fileSize += TS_SIZE;
							}
						}
						if (recordFile->Write(b, Count) < 0)
						{
							LOG_ERROR_STR(fileName->Name());
							break;
						}
						fileSize += Count;
						t = time(NULL);
					}
				}
				ringBuffer->Del(Count);
				liveBytesProcessed += Count;
			}
		}

		// if we still got live buffer to save, do so when the 75% of the bytes seen in live data have been processed
		if (m_ringBuffer != NULL && m_bufferWriter != NULL && !m_bufferWriter->Finished() && liveBytesProcessed >= 0.75 * liveByteCount)
		if (m_bufferWriter != NULL && !m_bufferWriter->Finished() && liveBytesProcessed >= 0.75 * liveByteCount && m_ringBuffer != NULL)
		{
			dsyslog("Permashift saving live buffer: count %d, processed %d", liveByteCount, liveBytesProcessed);

			liveBytesProcessed = 0;
			liveByteCount = 0;

			// I considered a sleep() after saving, but the file writing should be preempted anyway
			m_bufferWriter->SaveChunk();

			if (m_bufferWriter->Finished())
			{
				dsyslog("Permashift live buffer fully saved.");
				delete m_ringBuffer;
				m_ringBuffer = NULL;
			}
		}

		if (time(NULL) - t > MAXBROKENTIMEOUT)
		{
			esyslog("ERROR: video data stream broken");
			ShutdownHandler.RequestEmergencyExit();
			t = time(NULL);
		}
	}
}

bool cBufferReceiver::ActivatePreRecording(const char* fileName, int priority)
{
	bool retVal = true;

	if (fileName == NULL) return false;

	m_bufferSwitchMutex.Lock();

	m_bufferWriter = new cBufferWriter(m_ringBuffer, &m_frameIndex, fileName, m_saveOnTheFly);

	// initialize our recorder (writing to first free file number)
	InitializeFile(fileName, m_channel);
	cReceiver::SetPriority(priority);

	// redirect the data stream to our new receiver
	m_recordingMode = SyncingPhase;

	m_bufferSwitchMutex.Unlock();

	// wait for synchronization to finish
	m_syncCondition.Wait(0);

	return retVal;
}

bool cBufferReceiver::GetUsedBufferSecs(int* secs)
{
	if (secs == NULL) return false;

	if (m_frameIndex.Count() > 0 && frameDetector != NULL && frameDetector->FramesPerSecond() > 0)
	{
		*secs = m_frameIndex.Count() / frameDetector->FramesPerSecond();
		return true;
	}
	return false;
}
