/*
	Copyright 2009 Vincent Duvert, vincent.duvert@free.fr
	All rights reserved. Distributed under the terms of the MIT License.
*/

#include "VMWAddOnsTray.h"

#include <stdio.h>
#include <stdlib.h>

#include <Alert.h>
#include <Deskbar.h>
#include <Resources.h>
#include <Roster.h>
#include <MenuItem.h>
#include <Mime.h>

#include "VMWAddOns.h"
#include "VMWAddOnsSettings.h"
#include "vmwbackdoor.h"
#include "icons.h"

VMWAddOnsSettings settings;

extern "C" _EXPORT BView* instantiate_deskbar_item(void)
{
	return (new VMWAddOnsTray);
}

VMWAddOnsTray::VMWAddOnsTray()
	: BView(BRect(0, 0, B_MINI_ICON, B_MINI_ICON), 
		TRAY_NAME, B_FOLLOW_LEFT | B_FOLLOW_TOP, B_WILL_DRAW)
{
		init();
}

VMWAddOnsTray::VMWAddOnsTray(BMessage* mdArchive)
	:BView(mdArchive)
{
	init();
}

void
VMWAddOnsTray::init()
{
	entry_ref ref;
	
	system_clipboard = new BClipboard("system");
	clipboard_poller = NULL;
	window_open = false;
	
	icon_all = new BBitmap(BRect(0, 0, B_MINI_ICON - 1, B_MINI_ICON - 1), B_CMAP8);
	icon_all->SetBits(pic_act_yy, B_MINI_ICON * B_MINI_ICON, 0, B_CMAP8);
	
	icon_mouse = new BBitmap(BRect(0, 0, B_MINI_ICON - 1, B_MINI_ICON - 1), B_CMAP8);
	icon_mouse->SetBits(pic_act_ny, B_MINI_ICON * B_MINI_ICON, 0, B_CMAP8);
	
	icon_clipboard = new BBitmap(BRect(0, 0, B_MINI_ICON - 1, B_MINI_ICON - 1), B_CMAP8);
	icon_clipboard->SetBits(pic_act_yn, B_MINI_ICON * B_MINI_ICON, 0, B_CMAP8);
	
	icon_none = new BBitmap(BRect(0, 0, B_MINI_ICON - 1, B_MINI_ICON - 1), B_CMAP8);
	icon_none->SetBits(pic_act_nn, B_MINI_ICON * B_MINI_ICON, 0, B_CMAP8);
	
	SetDrawingMode(B_OP_ALPHA);
	SetFlags(Flags() | B_WILL_DRAW);
}

VMWAddOnsTray::~VMWAddOnsTray()
{
	delete icon_all;
	delete icon_mouse;
	delete icon_clipboard;
	delete icon_none;
	
	delete system_clipboard;
	delete clipboard_poller;
	
	if (window_open) {
		cleanup_window->Lock();
		cleanup_window->Close();
	}
}

void
VMWAddOnsTray::GetPreferredSize(float *w, float *h)
{
	*w = B_MINI_ICON;
	*h = B_MINI_ICON;
}

status_t
VMWAddOnsTray::Archive(BMessage *data, bool deep = true) const
{
	data->AddString("add_on", APP_SIG);

	return BView::Archive(data, deep);
}

VMWAddOnsTray*
VMWAddOnsTray::Instantiate(BMessage *data)
{
	return (new VMWAddOnsTray(data));
}

void
VMWAddOnsTray::Draw(BRect /*update_rect*/) {
	BRect tray_bounds(Bounds());
	
	if (Parent()) SetHighColor(Parent()->ViewColor());
	else SetHighColor(189, 186, 189, 255);
	FillRect(tray_bounds);
	
	if (settings.GetBool("mouse_enabled", true)) {
		if (settings.GetBool("clip_enabled", true))
			DrawBitmap(icon_all);
		else
			DrawBitmap(icon_mouse);
	} else {
		if (settings.GetBool("clip_enabled", true))
			DrawBitmap(icon_clipboard);
		else
			DrawBitmap(icon_none);
	}
}

void
VMWAddOnsTray::MouseDown(BPoint where)
{
	ConvertToScreen(&where);
	VMWAddOnsMenu* menu = new VMWAddOnsMenu(this);
	menu->Go(where, true, true, ConvertToScreen(Bounds()), true);
}

void
VMWAddOnsTray::MessageReceived(BMessage* message)
{
	switch(message->what) {
		case MOUSE_SHARING:
		{
			bool sharing_enabled = settings.GetBool("mouse_enabled", true);
			settings.SetBool("mouse_enabled", !sharing_enabled);
			this->Invalidate();
		}
		break;
		
		case CLIPBOARD_SHARING:
		{
			bool sharing_enabled = settings.GetBool("clip_enabled", true);
			settings.SetBool("clip_enabled", !sharing_enabled);
			SetClipboardSharing(!sharing_enabled);
			this->Invalidate();
		}
		break;
		
		case REMOVE_FROM_DESKBAR:
			RemoveMyself(true);
		break;
		
		case B_ABOUT_REQUESTED:
		{
			BAlert* alert = new BAlert("about", 
				APP_NAME ", version " APP_VERSION " © 2009, Vincent Duvert\n"
				"VMW Backdoor © 2006, Ken Kato\n"
				"Distributed under the terms of the MIT License.", "OK", NULL, NULL,
                B_WIDTH_AS_USUAL, B_OFFSET_SPACING, B_INFO_ALERT);
	        alert->SetShortcut(0, B_ENTER);
    	    alert->Go(NULL);
		}
		break;
	
		case B_CLIPBOARD_CHANGED:
		{
			char* data;
			ssize_t len;
			BMessage* clip_message = NULL;

			if (!system_clipboard->Lock())
				return;
			
			clip_message = system_clipboard->Data();
			if (clip_message == NULL) {
				system_clipboard->Unlock();
				return;
			}
				
			clip_message->FindData("text/plain", B_MIME_TYPE, (const void**)&data, &len);
			if(data == NULL) {
				system_clipboard->Unlock();
				return;
			}
			
			// Clear the host clipboard
			VMClipboardPaste(NULL, NULL);
			VMClipboardCopy(data, len);
			
			system_clipboard->Unlock();
		}
		break;
		
		case CLIPBOARD_POLL:
		{
			char* data;
			ssize_t len;
			
			if (VMClipboardPaste(&data, &len) != B_OK)
				return;
			
			if (len < 0)
				return;
		
			BMessage* clip_message = NULL;
			if (!system_clipboard->Lock()) {
				free(data);
				return;
			}
			system_clipboard->Clear();
			
			if (len > 0) {
				clip_message = system_clipboard->Data();
				if (clip_message == NULL) {
					free(data);
					system_clipboard->Unlock();
					return;
				}
				
				clip_message->AddData("text/plain", B_MIME_TYPE, data, len);
				system_clipboard->Commit();
				system_clipboard->Unlock();
			}
		}
	
		break;
		
		case SHRINK_DISKS:
		{
			int32 result = (new BAlert("Shrink disks",
				"Disk shrinking will operate on all auto-expanding disks "
				"attached to this virtual machine.\nFor best results it is "
				"recommanded to clean up free space on these disks before starting "
				"the process.", "Cancel", "Shrink now" B_UTF8_ELLIPSIS, 
					"Clean up disks" B_UTF8_ELLIPSIS, B_WIDTH_AS_USUAL, B_OFFSET_SPACING, 
						B_INFO_ALERT))->Go();
			
			if (result <= 0) // Cancel or quit
				return;
			
			if (result == 2) { // Clean up disks
				if (window_open) // The selection window is already open (should not happen)
					return;
				cleanup_window = new VMWAddOnsCleanupWindow(this);
				cleanup_window->Show();
				window_open = true;
				return;
			}
		}
		
		case START_SHRINK: // User clicked Shrink now (see above), or the cleanup process completed
		{
			int32 result = (new BAlert("Shrink disks",
				"The shrink operation will now be launched in VMWare."
				"This may take a long time ; the virtual machine will be "
				"suspended during the process.", "Cancel", "OK"))->Go();
			if (result == 1) {
				
			}
		}	
		break;
		
		case SHRINK_WINDOW_CLOSED:
			window_open = false;
		break;
		
		default:
			message->PrintToStream();
			BView::MessageReceived(message);
		break;
	}
}

void
VMWAddOnsTray::AttachedToWindow()
{
	SetClipboardSharing(settings.GetBool("clip_enabled", true));
}

void
VMWAddOnsTray::SetClipboardSharing(bool enable)
{
	if (clipboard_poller != NULL) delete clipboard_poller;
	clipboard_poller = NULL;
	
	if (enable) {
		system_clipboard->StartWatching(this);
		clipboard_poller = new BMessageRunner(this, new BMessage(CLIPBOARD_POLL), 1000000);
	} else {
		system_clipboard->StopWatching(this);
	}
}

long removeFromDeskbar(void *)
{	
	BDeskbar db;
	db.RemoveItem(TRAY_NAME);
		
	return 0;
}

void
VMWAddOnsTray::RemoveMyself(bool askUser)
{
	// From the BeBook : A BView sitting on the Deskbar cannot remove itself (or another view)
	// So we start another thread to do the dirty work
	
	int32 result = 1;
	if (askUser) {
		result = (new BAlert(TRAY_NAME,
		"Are you sure you want to quit ?\n"
		"This will stop clipboard sharing (but not mouse sharing if it is running).",
		"Cancel", "Quit", NULL, B_WIDTH_AS_USUAL, B_INFO_ALERT))->Go();
	}
	
	if (result != 0) {	
		thread_id th = spawn_thread(removeFromDeskbar, "goodbye cruel world", B_NORMAL_PRIORITY, NULL);
		if (th) resume_thread(th);
	}
}

VMWAddOnsMenu::VMWAddOnsMenu(VMWAddOnsTray* tray)
	:BPopUpMenu("tray_menu", false, false)
{
	BMenuItem* menu_item;

	SetFont(be_plain_font);
	
	menu_item = new BMenuItem("Enable mouse sharing", new BMessage(MOUSE_SHARING));
	menu_item->SetMarked(settings.GetBool("mouse_enabled", true));;
	AddItem(menu_item);
	
	menu_item = new BMenuItem("Enable clipboard sharing", new BMessage(CLIPBOARD_SHARING));
	menu_item->SetMarked(settings.GetBool("clip_enabled", true));
	AddItem(menu_item);

	if (!tray->window_open)
		AddItem(new BMenuItem("Shrink virtual disks " B_UTF8_ELLIPSIS, new BMessage(SHRINK_DISKS)));
	
	AddSeparatorItem();
	
	AddItem(new BMenuItem("About "APP_NAME B_UTF8_ELLIPSIS, new BMessage(B_ABOUT_REQUESTED)));
	AddItem(new BMenuItem("Quit" B_UTF8_ELLIPSIS, new BMessage(REMOVE_FROM_DESKBAR)));
		
	SetTargetForItems(tray);
	SetAsyncAutoDestruct(true);
}

VMWAddOnsMenu::~VMWAddOnsMenu()
{
}
