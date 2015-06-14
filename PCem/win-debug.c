#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP

#include <commctrl.h>

#include <stdio.h>

#include "nethandler.h"
#include "ibm.h"
#include "cpu.h"
#include "device.h"
#include "mem.h"
#include "model.h"
#include "resources.h"
#include "sound.h"
#include "video.h"
#include "vid_voodoo.h"
#include "vid_svga.h"
#include "vid_svga_render.h"
#include "win.h"
#include "fdc.h"
#include "win-display.h"

extern int is486;
static int romstolist[ROM_MAX], listtomodel[ROM_MAX], romstomodel[ROM_MAX], modeltolist[ROM_MAX];
static int settings_sound_to_list[20], settings_list_to_sound[20];
static int settings_network_to_list[20], settings_list_to_network[20];

HWND debug_hwnd;
int debug_is_open = 0;

// uint8_t *temp;

/* void load_log_from_file()
{
	int len = 0;
	if (pclogf)  fclose(pclogf);
	FILE *f = fopen("pclog.txt", "rb");
	if (f == NULL)
	{
		temp = (uint8_t *) malloc(1);
		temp[0] = 0;
		return;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	temp = (uint8_t *) malloc(len + 1);
	fread(temp, 1, len, f);
	temp[len] = 0;
	fclose(f);
	pclogf=fopen("pclog.txt","wt");
} */

static BOOL CALLBACK debug_dlgproc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
        char temp_str[256];
        HWND h;

        switch (message)
        {
                case WM_INITDIALOG:
                debug_is_open = 1;
                case WM_USER:
		// load_log_from_file();
                h = GetDlgItem(hdlg, IDC_DEBUGTEXT);
                SendMessage(h, WM_SETTEXT, 0, (LPARAM)rtlog);
                return TRUE;

                case WM_COMMAND:
                switch (LOWORD(wParam))
                {
			case IDOK:
                        case IDCANCEL:
			// free(temp);
                        debug_is_open = 0;
                        EndDialog(hdlg, 0);
                        return TRUE;
		}
	}

        return FALSE;
}

void debug_update()
{
	HWND h;

	if (debug_is_open)
	{
		h = GetDlgItem(debug_hwnd, IDC_DEBUGTEXT);
		SendMessage(h, WM_SETTEXT, 0, (LPARAM)rtlog);
                SendMessage(h, WM_VSCROLL, 0, SB_BOTTOM);
	}
}

void debug_open(HWND hwnd)
{
        debug_hwnd = CreateDialog(hinstance, TEXT("DebugDlg"), hwnd, debug_dlgproc);
        ShowWindow(debug_hwnd, SW_SHOW);
}
