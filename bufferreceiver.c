/*
 * Part of permashift, a plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */


#include "bufferreceiver.h"

#include <vdr/recording.h>
#include "permashift.h"

// copied from recording.c
#define MAXBROKENTIMEOUT 30000 // milliseconds


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
		// tell the plugin we're gone
		m_owner->BufferDeleted(this);
	}
	if (m_bufferWriter != NULL)
	{
		dsyslog("permashift: deleting buffer writer \n");
		delete m_bufferWriter;
		dsyslog("permashift: deleted buffer writer \n");
	}
	if (m_ringBuffer != NULL)
	{
		dsyslog("permashift: deleting ring buffer \n");
		delete m_ringBuffer;
		dsyslog("permashift: deleted ring buffer\n");
	}
	dsyslog("permashift: leaving CBufferReceiver destructor\n");
}

bool cBufferReceiver::Allocate(uint64_t bufferSize)
{
	// allocate ring buffer, rounding to TS package size
	return m_ringBuffer->Allocate(bufferSize / 188 * 188);
}

void cBufferReceiver::SetOwner(cPluginPermashift* owner)
{
	m_owner = owner;
}

void  cBufferReceiver::SetChannel(const cChannel *newChannel)
{
	m_channel = newChannel;

	// set receiver PIDs accordingly
	SetPids(m_channel);
}

void cBufferReceiver::SetSavingOnTheFly(bool saveOnTheFly)
{
	// only allowed to change when still prerecording to memory
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
		// signal to recording thread
		Cancel(3);
	}
}

void cBufferReceiver::Receive(
#if VDRVERSNUM > 20300
				const 
#endif
				uchar *Data, int Length)
{
	if (m_recordingMode == FileRecording)
	{
		// Use base class to pass data into the recorder buffer.
		// No need for a mutex lock as we're already in file recording phase.
		cRecorder::Receive(Data, Length);
		return;
	}
	
	// memory recording or switching phase

	// lock against phase switching in ActivatePreRecording()
	m_bufferSwitchMutex.Lock();

	// create frame detector at start of memory receiving
	if (frameDetector == NULL)
	{
		frameDetector = new cFrameDetector(m_channel->Vpid(), m_channel->Vtype());
	}

	// route the data through our sync buffer.
	m_syncBuffer.Put(Data, Length);

	int syncByteCount;
	uchar *syncBytes = m_syncBuffer.Get(syncByteCount);
	if (syncBytes != NULL)
	{
		// fetch analyzed data
		int Count = frameDetector->Analyze(syncBytes, syncByteCount);
		if (Count)
		{
			// suppose we're not switching to recording phase
			bool switchToRecorder = false;
			if (frameDetector->Synced())
			{
				if (frameDetector->NewFrame())
				{
					// if we're asked to switch to disc recording, we do so when synced and at a new I frame
					if (m_recordingMode == SyncingPhase && frameDetector->Synced() && frameDetector->IndependentFrame())
					{
						switchToRecorder = true;
					}
					else
					{
						// otherwise, add new frame information to our index
						m_frameIndex.Add(new tFrameInfo(frameDetector->IndependentFrame(), m_ringBuffer->BytesWritten()), m_frameIndex.Last());
					}
					// inject PAT/PMT to our ring buffer at new I frame
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
			}

			if (!switchToRecorder)
			{
				// transfer data to our ring buffer
				m_ringBuffer->WriteData(syncBytes, Count);
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
			}
			else
			{
				// switch to disc recording
				dsyslog("permashift: ending synchronization phase \n");

				m_recordingMode = FileRecording;

				// prepare buffer writer and index
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

				// write (whole or parts of) buffer
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

				// move rest of sync buffer to recorder buffer
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

				dsyslog("permashift: signaling end of synchronization phase \n");

				// signal end of sync phase
				m_syncCondition.Signal();
			}
		}
	}

	m_bufferSwitchMutex.Unlock();
}

// copied from recorder.c with minimal changes,
// except added live buffer saving
void cBufferReceiver::Action()
{
	cTimeMs t(MAXBROKENTIMEOUT);
	bool InfoWritten = false;
	bool FirstIframeSeen = true;
	int liveByteCount = 0;
	int liveBytesProcessed = 0;
	while (Running()) {
		int r;
		uchar *b = ringBuffer->Get(r);
		if (b) {
			if (liveByteCount == 0) {
				liveByteCount = r;
				}

			int Count = frameDetector->Analyze(b, r);
			if (Count) {
				if (!Running() && frameDetector->IndependentFrame()) // finish the recording before the next independent frame
					break;
				if (frameDetector->Synced()) {
					if (!InfoWritten) {
						cRecordingInfo RecordingInfo(recordingName);
						if (RecordingInfo.Read()) {
							if (frameDetector->FramesPerSecond() > 0 && DoubleEqual(RecordingInfo.FramesPerSecond(),DEFAULTFRAMESPERSECOND) && !DoubleEqual(RecordingInfo.FramesPerSecond(), frameDetector->FramesPerSecond())) {
								RecordingInfo.SetFramesPerSecond(frameDetector->FramesPerSecond());
								RecordingInfo.Write();
#if VDRVERSNUM > 20300
								LOCK_RECORDINGS_WRITE;
								Recordings->UpdateByName(recordingName);
#else
								Recordings.UpdateByName(recordingName);
#endif
								}
							}
						InfoWritten = true;
						// unsure about this, as the recording grows backward as well as forward...
						// cRecordingUserCommand::InvokeCommand(RUC_STARTRECORDING, recordingName);
						}
					if (FirstIframeSeen || frameDetector->IndependentFrame()) {
						FirstIframeSeen = true; // start recording with the first I-frame
						if (!NextFile())
							break;
						if (index && frameDetector->NewFrame())
							index->Write(frameDetector->IndependentFrame(), fileName->Number(), fileSize);
						if (frameDetector->IndependentFrame()) {
							recordFile->Write(patPmtGenerator.GetPat(), TS_SIZE);
							fileSize += TS_SIZE;
							int Index = 0;
							while (uchar *pmt = patPmtGenerator.GetPmt(Index)) {
								recordFile->Write(pmt, TS_SIZE);
								fileSize += TS_SIZE;
								}
							t.Set(MAXBROKENTIMEOUT);
							}
						if (recordFile->Write(b, Count) < 0) {
							LOG_ERROR_STR(fileName->Name());
							break;
							}
						fileSize += Count;
						}
					}
				ringBuffer->Del(Count);
				liveBytesProcessed += Count;
				}
			}

		// if we still got live buffer to save, do so when 75% of the bytes seen in live data have been processed
		if (m_ringBuffer != NULL && m_bufferWriter != NULL && !m_bufferWriter->Finished() && liveBytesProcessed >= 0.75 * liveByteCount)
		{
			dsyslog("permashift: saving chunk of buffer (%d of %d live bytes processed)", liveBytesProcessed, liveByteCount);

			// save some data
			m_bufferWriter->SaveChunk();

			// reset our live bytes counter
			liveBytesProcessed = 0;
			liveByteCount = 0;

			if (m_bufferWriter->Finished())
			{
				dsyslog("permashift: RAM recording fully saved.");
				delete m_ringBuffer;
				m_ringBuffer = NULL;
			}
		}

        if (t.TimedOut()) {
			esyslog("ERROR: video data stream broken");
			ShutdownHandler.RequestEmergencyExit();
			t.Set(MAXBROKENTIMEOUT);
			}
		}
}

bool cBufferReceiver::ActivatePreRecording(const char* fileName, int priority)
{
	bool retVal = true;

	if (fileName == NULL) return false;

	// sync against data receiving method
	m_bufferSwitchMutex.Lock();

	dsyslog("permashift: usage of preliminary RAM recording activated \n");

	// initialize our writer (which will create all video files needed for saving)
	m_bufferWriter = new cBufferWriter(m_ringBuffer, &m_frameIndex, fileName, m_saveOnTheFly);

	// initialize our recorder (writing to first free file number)
	dsyslog("permashift: starting disk recording of live video to come \n");
	InitializeFile(fileName, m_channel);
	cReceiver::SetPriority(priority);

	// starting sync phase
	dsyslog("permashift: starting synchronization phase \n");

	m_recordingMode = SyncingPhase;

	m_bufferSwitchMutex.Unlock();

	// wait for synchronization to finish
	m_syncCondition.Wait(0);

	dsyslog("permashift: end of synchronization phase acknowledged \n");

	return retVal;
}

bool cBufferReceiver::GetUsedBufferSecs(int* secs)
{
	if (secs == NULL) return false;

	// if we have got enough information, return number of frames in RAM divided by frames per second of video
	if (m_frameIndex.Count() > 0 && frameDetector != NULL && frameDetector->FramesPerSecond() > 0)
	{
		*secs = m_frameIndex.Count() / frameDetector->FramesPerSecond();
		return true;
	}
	// We leave secs as it is if we haven't got aanything useful ro return.
	// Caller might e.g. have set it to -1 to find out.
	return false;
}

