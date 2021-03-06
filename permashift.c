/*
 * Part of permashift, a plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "permashift.h"
#include "bufferreceiver.h"


static const char *VERSION        = "1.0.4";
static const char *DESCRIPTION    = trNOOP("Auto-buffer live TV");


// option names
static const char *MenuEntry_EnablePlugin = "EnablePlugin";
static const char *MenuEntry_MaxLength = "MaxTimeshiftLength";	// obsolete, but must be recognized for ignoring
static const char *MenuEntry_BufferSize = "MemoryBufferSizeMB";
static const char *MenuEntry_SaveOnTheFly = "SaveOnTheFly";

// option variables
const char *bufferSizeTexts[] = { "20 MB", "50 MB", "100 MB", "250 MB", "500 MB", "1 GB", "2 GB", "3 GB", "4 GB", "5 GB", "6 GB"};
const int bufferSizeCount = sizeof(bufferSizeTexts) / sizeof(const char *);
int bufferSizesInMB[bufferSizeCount] = { 20, 50, 100, 250, 500, 1 * 1024, 2 * 1024, 3 * 1024, 4 * 1024, 5 * 1024, 6 * 1024};
// default buffer size 100 MB, should not be too much for most systems and still make some usable rewind buffer of 1 or 2 minutes
int g_bufferSize = 100;
bool g_enablePlugin = true;
bool g_saveOnTheFly = true;


const char *cPluginPermashift::Version(void) { return VERSION; }
const char *cPluginPermashift::Description(void) { return tr(DESCRIPTION); }


cPluginPermashift::cPluginPermashift(void) : 
		m_statusMonitor(NULL), m_bufferReceiver(NULL)
{

}

cPluginPermashift::~cPluginPermashift()
{
	if (m_bufferReceiver != NULL)
	{
		delete m_bufferReceiver;
	}
	if (m_statusMonitor != NULL)
	{
		delete m_statusMonitor;
	}
}

bool cPluginPermashift::Start(void)
{
	m_statusMonitor = new LRStatusMonitor(this);
	return true;
}

void cPluginPermashift::Stop(void)
{
	// stop last recording
	StopLiveRecording();
}

void cPluginPermashift::ChannelSwitch(const cDevice *device, int channelNumber, bool liveView)
{
	if (g_enablePlugin)
	{
		if (liveView)
		{
			if (channelNumber > 0)
			{
				StartLiveRecording(channelNumber);
			}
			else
			{
				StopLiveRecording();
			}
		}
	}
}

bool cPluginPermashift::StartLiveRecording(int channelNumber)
{
#if VDRVERSNUM > 20300
	LOCK_CHANNELS_READ;
	const cChannel *channel = Channels->GetByNumber(channelNumber);
#else
	const cChannel *channel = Channels.GetByNumber(channelNumber);
#endif
	if (channel == NULL)
	{
		esyslog("permashift: did not find channel!");
		return false;
	}

	dsyslog("permashift: starting RAM recording\n");
	
	// create our receiver
	m_bufferReceiver = new cBufferReceiver();

	// allocate buffer memory (MBs rounded to multiple of TS package size 188)
	if (!m_bufferReceiver->Allocate((g_bufferSize * 1024ull * 1024) / 188 * 188))
	{
		delete m_bufferReceiver;
		m_bufferReceiver = NULL;
		esyslog("permashift: out of memory!");
		Skins.QueueMessage(mtError, tr("Permashift out of memory!"));
		return false;
	}

	// pass channel, options and a pointer to this plugin for callback
	m_bufferReceiver->SetChannel(channel);
	m_bufferReceiver->SetSavingOnTheFly(g_saveOnTheFly);
	m_bufferReceiver->SetOwner(this);

	// attach it as current receiver
	dsyslog("permashift: attaching our receiver\n");
	cDevice::ActualDevice()->AttachReceiver(m_bufferReceiver);

	dsyslog("permashift: started live recording\n");

	return true;
}

bool cPluginPermashift::StopLiveRecording()
{
	dsyslog("permashift: stopping live recording\n");
	
	// Check if it has been promoted and thus shouldn't be deleted by us.
	if (m_bufferReceiver != NULL && !m_bufferReceiver->IsPromoted())
	{
		dsyslog("permashift: deleting recording buffer\n");
		delete m_bufferReceiver;
	}

	// ... but we're "detaching" in any case
	m_bufferReceiver = NULL;

	dsyslog("permashift: stopped live recording\n");
	
	return true;
}

void cPluginPermashift::BufferDeleted(cBufferReceiver* callingReceiver)
{
	dsyslog("permashift: buffer deleted\n");
	
	// our buffer is about to be deleted by other means
	// only delete it if it is really our buffer and not an old promoted one!
	if (m_bufferReceiver == callingReceiver)
	{
		m_bufferReceiver = NULL;
	}
}

cMenuSetupPage *cPluginPermashift::SetupMenu(void)
{
	return new cMenuSetupLR();
}

bool cPluginPermashift::SetupParse(const char *Name, const char *Value)
{
	if (!strcmp(Name, MenuEntry_EnablePlugin))
	{
		g_enablePlugin = (0 == strcmp(Value, "1"));
		return true;
	}
	else if (!strcmp(Name, MenuEntry_MaxLength))
	{
		return true; // obsolete option, ignore it
	}
	else if (!strcmp(Name, MenuEntry_BufferSize))
	{
		if (isnumber(Value))
		{
			g_bufferSize = atoi(Value);
			return true;
		}
	}
	else if (!strcmp(Name, MenuEntry_SaveOnTheFly))
	{
		g_saveOnTheFly = (0 == strcmp(Value, "1"));
		return true;
	}
	return false;
}

bool cPluginPermashift::Service(const char* Id, void* Data)
{
	// pass seconds read into buffer available for rewinding
	if (strcmp(Id, "Permashift-GetUsedBufferSecs-v1") == 0)
	{
		if (Data != NULL)
		{
			if (m_bufferReceiver != NULL)
			{
				// get data but ignore return value,
				// we're supposed to return true either way as we know the service id
				(void)m_bufferReceiver->GetUsedBufferSecs((int*)Data);
			}
		}
		return true;
	}

	return false;
}

cMenuSetupLR::cMenuSetupLR()
{
	newEnablePlugin = g_enablePlugin;
	newBufferSizeIndex = 0;
	for (int i = 1; i < bufferSizeCount; i++)
	{
		if (bufferSizesInMB[i] <= g_bufferSize)
		{
			newBufferSizeIndex = i;
		}
	}
	newSaveBlocksRewind = !g_saveOnTheFly;

	Add(new cMenuEditBoolItem(tr("Enable plugin"), &newEnablePlugin));
	Add(new cMenuEditStraItem(tr("Memory buffer size"), &newBufferSizeIndex, bufferSizeCount, bufferSizeTexts));
	Add(new cMenuEditBoolItem(tr("Saving buffer blocks rewinding"), &newSaveBlocksRewind));
}

void cMenuSetupLR::Store(void)
{
	g_enablePlugin = newEnablePlugin;
	g_bufferSize = bufferSizesInMB[newBufferSizeIndex];
	g_saveOnTheFly = !newSaveBlocksRewind;

	SetupStore(MenuEntry_EnablePlugin, newEnablePlugin);
	SetupStore(MenuEntry_BufferSize, g_bufferSize);
	SetupStore(MenuEntry_SaveOnTheFly, g_saveOnTheFly);
}


void LRStatusMonitor::ChannelSwitch(const cDevice *device, int channelNumber, bool liveView)
{
	m_plugin->ChannelSwitch(device, channelNumber, liveView);
}

VDRPLUGINCREATOR(cPluginPermashift); // Don't touch this!
