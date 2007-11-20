#include <Carbon/Carbon.h>
//#include <Skype/Skype.h>
#include <glib.h>
#include <CoreFoundation/CoreFoundation.h>

#include "AutoreleasePoolInit.h"

//change this to 0 if using an old version of the Skype.framework
#define SENDSKYPERETURNS 0

#include "skype_messaging_carbon2.c"

static gboolean connected_to_skype = FALSE;

void
SkypeNotificationReceived(CFStringRef input)
{
	char *output = NULL;
	void *pool = initAutoreleasePool();
	int strlen = CFStringGetMaximumSizeForEncoding(CFStringGetLength(input), kCFStringEncodingUTF8);
	
	printf("Message received");
	output = (char *)CFStringGetCStringPtr(input, kCFStringEncodingUTF8);
	if (!output)
	{
		output = NewPtr(strlen+1);
		CFStringGetCString(input, output, strlen+1, kCFStringEncodingUTF8);
	}
	printf(" %s\n", output);
	g_thread_create((GThreadFunc)skype_message_received, (void *)output, FALSE, NULL);
	
	destroyAutoreleasePool(pool);
}

void
SkypeAttachResponse(unsigned int aAttachResponseCode)
{
	if (aAttachResponseCode)
	{
		printf("Skype attached successfully :)\n");
		connected_to_skype = TRUE;
	}
	else
	{
		printf("Skype couldn't connect :(\n");
		connected_to_skype = FALSE;
	}
}

void
SkypeBecameAvailable(CFPropertyListRef aNotification)
{
	printf("Skype became available\n");
	connected_to_skype = TRUE;
}

void
SkypeBecameUnavailable(CFPropertyListRef aNotification)
{
	printf("Skype became unavailable\n");
	connected_to_skype = FALSE;
	g_thread_create((GThreadFunc)skype_message_received, "CONNSTATUS LOGGEDOUT", FALSE, NULL);
}

static struct SkypeDelegate skypeDelegate = {
	CFSTR("Adium"), /* clientAppName */
	SkypeNotificationReceived,
	SkypeAttachResponse,
	SkypeBecameAvailable,
	SkypeBecameUnavailable
};

static gboolean
skype_connect_thread(gpointer data)
{
	void *pool = initAutoreleasePool();

	printf("Start inner event loop\n");
	RunApplicationEventLoop();
	printf("End of event loop\n");
	
	destroyAutoreleasePool(pool);
	
	//don't loop this thread
	return FALSE;
}

static gpointer static_pool;

static gboolean
skype_connect()
{
	gboolean is_skype_running = FALSE;
	
	static_pool = initAutoreleasePool();
	
	is_skype_running = IsSkypeRunning();
	
	printf("Is Skype running? '%s'\n", (is_skype_running?"Yes":"No"));
	if (!is_skype_running)
		return FALSE;
		
	SetSkypeDelegate(&skypeDelegate);
	ConnectToSkype();
	
	g_thread_create((GThreadFunc)skype_connect_thread, NULL, FALSE, NULL);
	while(connected_to_skype == FALSE)
	{
		RunCurrentEventLoop(1);
	}
	printf("Connected to skype\n");
	return TRUE;
}

static void
skype_disconnect()
{
	connected_to_skype = FALSE;
	DisconnectFromSkype();
	RemoveSkypeDelegate();
	RunCurrentEventLoop(1);
	
	destroyAutoreleasePool(static_pool);
}

static void
send_message(char* message)
{
	if (!connected_to_skype)
	{
		if (message[0] == '#')
		{
			int message_num;
			char error_return[40];
			//And we're expecting a response
			sscanf(message, "#%d ", &message_num);
			sprintf(error_return, "#%d ERROR", message_num);
			g_thread_create((GThreadFunc)skype_message_received, (void *)g_strdup(error_return), FALSE, NULL);
		}
		return;
	}

	gpointer pool = initAutoreleasePool();
	printf("Skype send message ");
#if SENDSKYPERETURNS
	CFStringRef returnString = NULL;
	returnString = SendSkypeCommand(CFStringCreateWithCString(NULL, message, kCFStringEncodingUTF8));
	if (returnString)
		SkypeNotificationReceived(returnString);
#else
	SendSkypeCommand(CFStringCreateWithCString(NULL, message, kCFStringEncodingUTF8));
#endif
	destroyAutoreleasePool(pool);
	printf("%s\n", message);
}

static void
hide_skype()
{
	OSStatus status = noErr;
	ProcessSerialNumber psn = {kNoProcess, kNoProcess};
	unsigned int procNameLength = 32;
	unsigned char procName[procNameLength];
	unsigned int i = 0;
	ProcessInfoRec info;
	info.processInfoLength = sizeof(ProcessInfoRec);
	info.processName = procName;
	info.processAppSpec = NULL;
	
	while(status == noErr)
	{
		for(i = 0; i < procNameLength; i++)
			procName[i] = '\0';
		
		status = GetNextProcess(&psn);
		if (status == noErr)
			if (GetProcessInformation(&psn, &info) == noErr)
				//for some reason first character is poisioned
				if (strcmp((char *)&procName[1], "Skype") == 0)
				{
					ShowHideProcess(&psn, FALSE);
					return;
				}
	}
}

