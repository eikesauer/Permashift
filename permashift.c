/*
 * Part of permashift, a plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */


#include "permashift.h"
#include "bufferreceiver.h"

static const char *VERSION        = "1.0.1";
static const char *DESCRIPTION    = trNOOP("Auto-buffer live TV");


// #define EXPIRECANCELPROMPT    5 // seconds to wait in user prompt before expiring recording

// option names
static const char *MenuEntry_EnablePlugin = "EnablePlugin";
static const char *MenuEntry_MaxLength = "MaxTimeshiftLength";
static const char *MenuEntry_BufferSize = "MemoryBufferSizeMB";
static const char *MenuEntry_SaveOnTheFly = "SaveOnTheFly";

// option variables
const char *bufferSizeTexts[] = { "20 MB", "50 MB", "100 MB", "250 MB", "500 MB", "1 GB", "2 GB", "3 GB", "4 GB", "5 GB", "6 GB"};
const int bufferSizeCount = sizeof(bufferSizeTexts) / sizeof(const char *);
int bufferSizesInMB[bufferSizeCount] = { 20, 50, 100, 250, 500, 1 * 1024, 2 * 1024, 3 * 1024, 4 * 1024, 5 * 1024, 6 * 1024};
// default buffer size 100 MB, should not be too much for most systems and still make some usable rewind buffer even for HD
int g_bufferSize = 100;
bool g_enablePlugin = true;
// obsolete		int g_maxLength = 3;
bool g_saveOnTheFly = true;

const char *cPluginPermashift::Version(void) { return VERSION; }
const char *cPluginPermashift::Description(void) { return tr(DESCRIPTION); }


cPluginPermashift::cPluginPermashift(void) : 
		m_statusMonitor(NULL), m_bufferReceiver(NULL) /*, m_mainThreadCounter(0) */
{
	// obsolete		g_maxLength = Setup.InstantRecordTime / 60;
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

// Option: enabling plugin
void cPluginPermashift::SetEnable(bool enable)
{
	if (!enable)
	{
		StopLiveRecording();
	}
	g_enablePlugin = enable;
};

bool cPluginPermashift::IsEnabled(void)
{
	return g_enablePlugin;
};

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

/*
void cPluginPermashift::MainThreadHook(void)
{
	// This hook is supposed to be called about once a second,
	// so let's do our checks about once a minute.
	if (m_mainThreadCounter++ >= 60)
	{
		if (m_bufferReceiver != NULL)
		{
			if (ShutdownHandler.IsUserInactive())
			{
#ifdef EXPIRECANCELPROMPT
				if (Interface->Confirm(tr("Press key to continue permanent timeshift"), EXPIRECANCELPROMPT, true))
#endif
				{
					StopLiveRecording();
				}
			}
		}
		m_mainThreadCounter = 0;
	}
}
*/

void cPluginPermashift::ChannelSwitch(const cDevice *device, int channelNumber, bool liveView)
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

bool cPluginPermashift::StartLiveRecording(int channelNumber)
{
	if (!g_enablePlugin) return true;

	cChannel *channel = Channels.GetByNumber(channelNumber);
	if (channel == NULL)
	{
		esyslog("Permashift: Did not find channel!");
		return false;
	}

	// ? cDevice::ActualDevice()->ForceTransferMode();

	// Start recording
	m_bufferReceiver = new cBufferReceiver();

	// Allocate buffer memory (MBs rounded to multiple of TS package size 188)
	if (!m_bufferReceiver->Allocate((g_bufferSize * 1024ull * 1024) / 188 * 188))
	{
		delete m_bufferReceiver;
		m_bufferReceiver = NULL;
		esyslog("Permashift out of memory!");
		Skins.QueueMessage(mtError, tr("Permashift out of memory!"));
		return false;
	}

	m_bufferReceiver->SetChannel(channel);
	m_bufferReceiver->SetSavingOnTheFly(g_saveOnTheFly);
	m_bufferReceiver->SetOwner(this);

	cDevice::ActualDevice()->AttachReceiver(m_bufferReceiver);

	return true;
}

bool cPluginPermashift::StopLiveRecording()
{
	if (!g_enablePlugin) return true;

	// Check if it has been promoted and thus shouldn't be deleted by us.
	if (m_bufferReceiver != NULL && !m_bufferReceiver->IsPromoted())
	{
		delete m_bufferReceiver;
	}

	// ... but we're "detaching" in any case
	m_bufferReceiver = NULL;

	return true;
}

void cPluginPermashift::BufferDeleted()
{
	// our buffer is about to be deleted by other means
	m_bufferReceiver = NULL;
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
		return true; // we know it, but we ignore it
		/*
		if (isnumber(Value))
		{
			g_maxLength = atoi(Value);
			return true;
		}
		*/
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
	if (strcmp(Id, "Permashift-GetUsedBufferSecs-v1") == 0)
	{
		if (Data != NULL)
		{
			if (m_bufferReceiver != NULL)
			{
				m_bufferReceiver->GetUsedBufferSecs((int*)Data);
			}
		}
		return true;
	}

	return false;
}

cMenuSetupLR::cMenuSetupLR()
{
	newEnablePlugin = g_enablePlugin;
	// obsolete		newMaxLength = g_maxLength;
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
	// obsolete		Add(new cMenuEditIntItem(tr("Maximum recording length (hours)"), &newMaxLength, 1, 23));
	Add(new cMenuEditStraItem(tr("Memory buffer size"), &newBufferSizeIndex, bufferSizeCount, bufferSizeTexts));
	Add(new cMenuEditBoolItem(tr("Saving buffer blocks rewinding"), &newSaveBlocksRewind));
}

void cMenuSetupLR::Store(void)
{
	g_enablePlugin = newEnablePlugin;
	// obsolete		g_maxLength = newMaxLength;
	g_bufferSize = bufferSizesInMB[newBufferSizeIndex];
	g_saveOnTheFly = !newSaveBlocksRewind;

	SetupStore(MenuEntry_EnablePlugin, newEnablePlugin);
	// obsolete		SetupStore(MenuEntry_MaxLength, newMaxLength);
	SetupStore(MenuEntry_BufferSize, g_bufferSize);
	SetupStore(MenuEntry_SaveOnTheFly, g_saveOnTheFly);
}


void LRStatusMonitor::ChannelSwitch(const cDevice *device, int channelNumber, bool liveView)
{
	m_plugin->ChannelSwitch(device, channelNumber, liveView);
}

VDRPLUGINCREATOR(cPluginPermashift); // Don't touch this!
