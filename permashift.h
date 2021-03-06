/*
 * Part of permashift, a plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */


#include <vdr/plugin.h>
#include <vdr/status.h>
#include <vdr/menu.h>
#include <vdr/timers.h>
#include <vdr/shutdown.h>
#include <vdr/interface.h>

class cPluginPermashift;
class cBufferReceiver;

/// Setup menu class
class cMenuSetupLR : public cMenuSetupPage 
{
private:
	int newEnablePlugin;
	int newBufferSizeIndex;
	int newSaveBlocksRewind;

protected:
	virtual void Store(void);

public:
	cMenuSetupLR();
};


/// Status class
class LRStatusMonitor : public cStatus
{
private:
	cPluginPermashift* m_plugin;

public:
	LRStatusMonitor(cPluginPermashift* plugin)
	{
		m_plugin = plugin;
	}

protected:

	virtual void ChannelSwitch(const cDevice *device, int channelNumber, bool liveView);

};


/// permashift plugin class
class cPluginPermashift : public cPlugin
{
private:
	// our status monitor
	LRStatusMonitor *m_statusMonitor;

	// memory buffer receiver
	cBufferReceiver* m_bufferReceiver;

public:

	cPluginPermashift(void);
	virtual ~cPluginPermashift();

	/// our buffer tells us that it's gone
	void BufferDeleted(cBufferReceiver* callingReceiver);

	/// status callback
	void ChannelSwitch(const cDevice *device, int channelNumber, bool liveView);

	// plugin overrides
	virtual bool Start(void);
	virtual void Stop(void);
	virtual const char *Version(void);
	virtual const char *Description(void);
	virtual cMenuSetupPage *SetupMenu(void);
	virtual bool SetupParse(const char *Name, const char *Value);

	/// Service "Permashift-GetUsedBufferSecs-v1", called with int*.
	/// Will receive the seconds read into buffer available for rewinding.
	/// If there's no useful value (yet), secs will remain unchanged.
	bool Service(const char* Id, void* Data);

private:

	/// start a recording
	bool StartLiveRecording(int channelNumber);

	/// stop a recording
	bool StopLiveRecording(void);

};
