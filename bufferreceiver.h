#ifndef BUFFERRECEIVER_H
#define BUFFERRECEIVER_H

#include "overwritingringbuffer.h"
#include "bufferwriter.h"

#include <vdr/recorder.h>


class cPluginPermashift;

/// frame index data
class tFrameInfo : public cListObject
{
public:

	bool iFrame;		 ///< I frame or not
	uint64_t offset;	 ///< offset, relative to first data written to buffer
	unsigned int fileNo; ///< file number to write to (if splitting)

	tFrameInfo(bool iFrame, uint64_t offset)
	{
		this->iFrame = iFrame;
		this->offset = offset;
		fileNo = 1;
	}
};

/// recorder class using buffer, or file if switched
class cBufferReceiver : public cRecorder
{
private:

	/// syncs receive method vs. activation of recording
	cMutex m_bufferSwitchMutex;
	
	/// used by ActivatePreRecording() to wait for end of buffer switching and parts of file ot be saved
	cCondWait m_syncCondition;

	/// channel to record
	const cChannel *m_channel;

	/// phase of recording
	enum
	{
		MemoryRecording,	///< recording to memory
		SyncingPhase,		///< switching to file recording
		FileRecording		///< recording to file
	} m_recordingMode;

	/// data container class
	cOverwritingRingBuffer *m_ringBuffer;

	/// list of frame information data for frames in buffer
	/// (dropped frames will be dropped from this list as well)
	cList<tFrameInfo> m_frameIndex;

	/// used to write buffer to file
	cBufferWriter* m_bufferWriter;

	/// used for keeping TS data in synchronization phase
	cRingBufferLinear m_syncBuffer;

	// option: should saving be done on-the-fly?
	bool m_saveOnTheFly;

	/// our owner, which needs to be informed when we're deleted
	/// (probably not a good design...)
	cPluginPermashift* m_owner;

public:

	cBufferReceiver();
	~cBufferReceiver();

	/// try to allocate the buffer, returns false if failed
	bool Allocate(uint64_t bufferSize);

	/// set channel to receive
	void SetChannel(const cChannel *channel);

	/// sets saving on the fly (only works as long as the recording has not been used)
	void SetSavingOnTheFly(bool saveOnTheFly);

	/// connect to our owning class
	void SetOwner(cPluginPermashift* owner);

	/// is already being used as recording
	bool IsPromoted() { return m_recordingMode != MemoryRecording; }

	/// receiver is recording the given channel (and has not yet been used as recording)
	bool IsPreRecording(const cChannel *Channel);

	/// saves the buffer contents to file
	bool ActivatePreRecording(const char* fileName, int Priority);

	/// queries seconds of video recorded at the moment
	bool GetUsedBufferSecs(int* secs);

protected:

	/// (de-)activation
	virtual void Activate(bool On);

	/// hands over some data
	virtual void Receive(
#if VDRVERSNUM > 20300	  
				const 
#endif
				uchar *Data, int Length);

private:

	// recording thread when saving to file
	void Action();

};

#endif // BUFFERRECEIVER_H
