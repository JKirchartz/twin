/*
 *  builtin.c  --  create default menu, Clock and About windows.
 *
 *  Copyright (C) 1993-2001 by Massimiliano Ghilardi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <signal.h>
#include <unistd.h>

#include "twin.h"
#include "data.h"
#include "methods.h"
#include "main.h"
#include "extensions.h"

#include "hw.h"
#include "hw_multi.h"
#include "resize.h"
#include "draw.h"
#include "printk.h"
#include "util.h"
#include "version.h"

#ifdef CONF__MODULES
# include "dl.h"
#endif

#include "Tw/Twkeys.h"


#define COD_QUIT	(udat)1 /* as in term.c */
#define COD_SPAWN	(udat)3 /* as COD_SPAWN in term.c */

#define COD_EXECUTE	(udat)5
#define COD_SUSPEND	(udat)10
#define COD_DETACH	(udat)11
#define COD_RELOAD_RC	(udat)12

#define COD_CLOCK_WIN   (udat)20
#define COD_OPTION_WIN	(udat)21
#define COD_BUTTONS_WIN	(udat)22
#define COD_DISPLAY_WIN	(udat)23
#define COD_MESSAGES_WIN (udat)24
#define COD_ABOUT_WIN	(udat)25


#define COD_TERM_ON	(udat)30
#define COD_TERM_OFF	(udat)31
#define COD_SOCKET_ON	(udat)32
#define COD_SOCKET_OFF	(udat)33

#define COD_O_SHADOWS	(udat)40
#define COD_O_Xp_SHADE	(udat)41
#define COD_O_Xn_SHADE	(udat)42
#define COD_O_Yp_SHADE	(udat)43
#define COD_O_Yn_SHADE	(udat)44
#define COD_O_BLINK	(udat)45
#define COD_O_ALWAYSCURSOR (udat)46
#define COD_O_HIDEMENU	(udat)47
#define COD_O_MENUINFO	(udat)48
#define COD_O_EDGESCROLL (udat)49
#define COD_O_ALTFONT	(udat)50

#define COD_D_REMOVE	(udat)60
#define COD_D_THIS	(udat)61

#define COD_E_TTY	(udat)70

msgport Builtin_MsgPort;

static menu Builtin_Menu;
static menuitem Builtin_File;
#if defined(CONF__MODULES) && !(defined(CONF_SOCKET) && defined(CONF_TERM))
static menuitem Builtin_Modules;
#endif

static window AboutWin, ClockWin, OptionWin, ButtonWin,
    DisplayWin, DisplaySubWin, ExecuteWin;

window WinList, MessagesWin;

static gadget ButtonOK_About, ButtonRemove, ButtonThis;

static void Clock_Update(void) {
    timevalue *Time = &All->Now;
    struct tm *Date;
    byte Buffer[30];
    
    ClockWin->CurX=ClockWin->CurY=(uldat)0;
    Date = localtime(&Time->Seconds);
    
    sprintf((char *)Buffer, "%02u/%02u/%04u\n %02u:%02u:%02u",
	    (udat)Date->tm_mday, (udat)Date->tm_mon+1, (udat)Date->tm_year + 1900,
	    (udat)Date->tm_hour, (udat)Date->tm_min,   (udat)Date->tm_sec);
    Act(WriteRow,ClockWin)(ClockWin, strlen(Buffer), Buffer);
    
    Builtin_MsgPort->PauseDuration.Fraction = 1 FullSECs - Time->Fraction;
    Builtin_MsgPort->PauseDuration.Seconds = 0;
}

#if defined(CONF__MODULES) && !(defined(CONF_TERM) && defined(CONF_SOCKET))
static void TweakMenuRows(menuitem Item, udat code, byte flag) {
    window Win;
    row Row;
    
    if ((Win = Item->Window) &&
	(Row = Act(FindRowByCode,Win)(Win, code, (uldat *)0)))
	Row->Flags = flag;
}

static void UpdateMenuRows(window dummy) {
#ifndef CONF_TERM
    if (DlIsLoaded(TermSo)) {
	TweakMenuRows(Builtin_Modules, COD_TERM_ON,    ROW_INACTIVE);
	TweakMenuRows(Builtin_Modules, COD_TERM_OFF,   ROW_ACTIVE);
    } else {
	TweakMenuRows(Builtin_Modules, COD_TERM_ON,    ROW_ACTIVE);
	TweakMenuRows(Builtin_Modules, COD_TERM_OFF,   ROW_INACTIVE);
    }
#endif
#ifndef CONF_SOCKET
    if (DlIsLoaded(SocketSo)) {
	TweakMenuRows(Builtin_Modules, COD_SOCKET_ON,  ROW_INACTIVE);
	TweakMenuRows(Builtin_Modules, COD_SOCKET_OFF, ROW_ACTIVE);
    } else {
	TweakMenuRows(Builtin_Modules, COD_SOCKET_ON,  ROW_ACTIVE);
	TweakMenuRows(Builtin_Modules, COD_SOCKET_OFF, ROW_INACTIVE);
    }
#endif
}

#endif


static void SelectWinList(void) {
    screen Screen = All->FirstScreen;
    uldat n = WinList->CurY;
    widget W;
    
    for (W = Screen->FirstW; W; W = W->Next) {
	if (W == (widget)WinList || !IS_WINDOW(W) || (((window)W)->Attrib & WINDOW_MENU))
	    continue;
	if (!n)
	    break;
	n--;
    }
    if (!n && W) {
	MakeFirstWindow((window)W, TRUE);
	CenterWindow((window)W);
    }
}


static void ExecuteGadgetH(event_gadget *EventG) {
    gadget G;
    
    if (EventG->Code == COD_E_TTY &&
	(G = Act(FindGadgetByCode,ExecuteWin)(ExecuteWin, COD_E_TTY))) {
	
	if (G->Text[0][1] == ' ')
	    G->Text[0][1] = '�';
	else
	    G->Text[0][1] = ' ';
	
	DrawAreaWidget((widget)G);
    }
}

static void ExecuteWinRun(void) {
    byte **argv, *arg0;
    row Row;
    gadget G;
    
    Act(UnMap,ExecuteWin)(ExecuteWin);
    
    if ((Row = Act(FindRow,ExecuteWin)(ExecuteWin, ExecuteWin->CurY)) && !Row->LenGap) {
	
	argv = TokenizeStringVec(Row->Len, Row->Text);
	arg0 = argv ? argv[0] : NULL;

	if ((G = Act(FindGadgetByCode,ExecuteWin)(ExecuteWin, COD_E_TTY)) && G->Text[0][1] != ' ') {
	    /* run in a tty */
	    Ext(Term,Open)(arg0, argv);
	} else if (argv) switch (fork()) {
	    /* do not run in a tty */
	  case -1: /* error */
	    break;
	  case 0:  /* child */
	    execvp((char *)arg0, (char **)argv);
	    exit(1);
	    break;
	  default: /* parent */
	    break;
	}
	if (argv)
	    FreeStringVec(argv);
    }
}

void UpdateOptionWin(void) {
    gadget G;
    byte i, Flags = All->SetUp->Flags;
    udat list[] = {COD_O_Xp_SHADE, COD_O_Xn_SHADE, COD_O_Yp_SHADE, COD_O_Yn_SHADE, 0 };

    for (i = 0; list[i]; i++) {
	if ((G = Act(FindGadgetByCode,OptionWin)(OptionWin, list[i]))) {
	    if (Flags & SETUP_SHADOWS)
		G->Flags &= ~GADGET_DISABLED;
	    else
		G->Flags |= GADGET_DISABLED;
	}
    }
    if ((G = Act(FindGadgetByCode,OptionWin)(OptionWin, COD_O_SHADOWS)))
	G->Text[0][1] = Flags & SETUP_SHADOWS ? '�' : ' ';
    if ((G = Act(FindGadgetByCode,OptionWin)(OptionWin, COD_O_ALWAYSCURSOR)))
	G->Text[0][1] = Flags & SETUP_ALWAYSCURSOR ? '�' : ' ';
    if ((G = Act(FindGadgetByCode,OptionWin)(OptionWin, COD_O_BLINK)))
	G->Text[0][1] = Flags & SETUP_BLINK ? '�' : ' ';
    if ((G = Act(FindGadgetByCode,OptionWin)(OptionWin, COD_O_HIDEMENU)))
	G->Text[0][1] = Flags & SETUP_HIDEMENU ? '�' : ' ';
    if ((G = Act(FindGadgetByCode,OptionWin)(OptionWin, COD_O_MENUINFO)))
	G->Text[0][1] = Flags & SETUP_MENUINFO ? '�' : ' ';
    if ((G = Act(FindGadgetByCode,OptionWin)(OptionWin, COD_O_EDGESCROLL)))
	G->Text[0][1] = Flags & SETUP_EDGESCROLL ? '�' : ' ';
    if ((G = Act(FindGadgetByCode,OptionWin)(OptionWin, COD_O_ALTFONT)))
	G->Text[0][1] = Flags & SETUP_ALTFONT ? '�' : ' ';
    
    OptionWin->CurX = 25; OptionWin->CurY = 1;
    i = (Flags & SETUP_SHADOWS ? All->SetUp->DeltaXShade : 0) + '0';
    Act(WriteRow,OptionWin)(OptionWin, 1, &i);
    OptionWin->CurX = 25; OptionWin->CurY = 2;
    i = (Flags & SETUP_SHADOWS ? All->SetUp->DeltaYShade : 0) + '0';
    Act(WriteRow,OptionWin)(OptionWin, 1, &i);
}


static void OptionH(msg Msg) {
    byte Flags = All->SetUp->Flags, XShade = All->SetUp->DeltaXShade, YShade = All->SetUp->DeltaYShade;
    byte redraw = TRUE;
    
    switch (Msg->Event.EventGadget.Code) {
      case COD_O_ALTFONT:
	Flags ^= SETUP_ALTFONT;
	ResetBorderPattern();
	break;
      case COD_O_SHADOWS:
	Flags ^= SETUP_SHADOWS;
	break;
      case COD_O_Xp_SHADE:
	if (XShade < MAX_XSHADE)
	    XShade++;
	break;
      case COD_O_Xn_SHADE:
	if (XShade > 1)
	    XShade--;
	break;
      case COD_O_Yp_SHADE:
	if (YShade < MAX_YSHADE)
	    YShade++;
	break;
      case COD_O_Yn_SHADE:
	if (YShade > 1)
	    YShade--;
	break;
      case COD_O_BLINK:
	Flags ^= SETUP_BLINK;
	break;
      case COD_O_ALWAYSCURSOR:
	Flags ^= SETUP_ALWAYSCURSOR;
	redraw = FALSE;
	break;
      case COD_O_HIDEMENU:
	Flags ^= SETUP_HIDEMENU;
	HideMenu(!!(Flags & SETUP_HIDEMENU));
	redraw = FALSE;
	break;
      case COD_O_MENUINFO:
	Flags ^= SETUP_MENUINFO;
	break;
      case COD_O_EDGESCROLL:
	Flags ^= SETUP_EDGESCROLL;
	redraw = FALSE;
	break;
      default:
	redraw = FALSE;
	break;
    }
    if (Flags != All->SetUp->Flags || XShade != All->SetUp->DeltaXShade
	|| YShade != All->SetUp->DeltaYShade) {
	
	All->SetUp->Flags = Flags;
	All->SetUp->DeltaXShade = XShade;
	All->SetUp->DeltaYShade = YShade;
	
	UpdateOptionWin();
	if (redraw == TRUE)
	    QueuedDrawArea2FullScreen = TRUE;
	else {
	    DrawFullWindow2(OptionWin);
	    UpdateCursor();
	}
    }
}

void FillButtonWin(void) {
    byte i, j;
    byte b[6] = "      ", *s;
    
    DeleteList(ButtonWin->FirstW);
    DeleteList(ButtonWin->USE.R.FirstRow);
    
    for (i=j=0; j<BUTTON_MAX; j++) {
	if (!All->ButtonVec[j].exists)
	    continue;
	
	ButtonWin->CurX = 2; ButtonWin->CurY = 1 + i*2;
	if (j)
	    b[2] = j + '0', s = b;
	else
	    s = "Close ";
	Act(WriteRow,ButtonWin)(ButtonWin, 7, "Button ");
	Act(WriteRow,ButtonWin)(ButtonWin, 6, s);
	Act(WriteRow,ButtonWin)(ButtonWin, 2, All->ButtonVec[j].shape);

	Do(Create,Gadget)(FnGadget, Builtin_MsgPort, (widget)ButtonWin, 3, 1, "[-]",
			  2 | (j<<2), GADGET_USE_DEFCOL,
			  COL(BLACK,WHITE), COL(HIGH|WHITE,GREEN),
			  COL(HIGH|BLACK,WHITE), COL(HIGH|BLACK,BLACK),
			  19, 1+i*2, 
			  NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	Do(Create,Gadget)(FnGadget, Builtin_MsgPort, (widget)ButtonWin, 3, 1, "[+]",
			  3 | (j<<2), GADGET_USE_DEFCOL,
			  COL(BLACK,WHITE), COL(HIGH|WHITE,GREEN),
			  COL(HIGH|BLACK,WHITE), COL(HIGH|BLACK,BLACK),
			  22, 1+i*2,
			  NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	i++;
    }
    
    ResizeRelWindow(ButtonWin, 0, (dat)(3 + i*2) - (dat)ButtonWin->YWidth);
}
    
void UpdateButtonWin(void) {
    byte i, j, s[5];
    num pos;
    
    for (i=j=0; j<BUTTON_MAX; j++) {
	if (!All->ButtonVec[j].exists)
	    continue;
	ButtonWin->CurX = 26; ButtonWin->CurY = 1 + i*2;
	
	pos = All->ButtonVec[j].pos;
	if (pos >= 0) {
	    Act(WriteRow,OptionWin)(ButtonWin, 5, "Left ");
	} else if (pos == -1)
	    Act(WriteRow,OptionWin)(ButtonWin, 9, "Disabled ");
	else {
	    Act(WriteRow,OptionWin)(ButtonWin, 5, "Right");
	    pos = -pos - 2;
	}
	if (pos >= 0) {
	    sprintf(s, " %3d", pos);
	    Act(WriteRow,OptionWin)(ButtonWin, strlen(s), s);
	}
	i++;
    }
}

static void BordersH(msg Msg) {
    udat Code = Msg->Event.EventGadget.Code;
    num op = -1;

    if (!(Code & 2))
	return;
    
    if (Code & 1)
	op = +1;
    
    All->ButtonVec[Code >> 2].pos += op;
    
    QueuedDrawArea2FullScreen = TRUE;
    UpdateButtonWin();
}

static void UpdateDisplayWin(window displayWin) {
    display_hw hw;
    uldat x = 12, y = 0;
    
    if (displayWin == DisplayWin) {
	DeleteList(DisplayWin->USE.R.FirstRow);
    
	for (hw = All->FirstDisplayHW; hw; hw = hw->Next) {
	    Act(GotoXY,DisplayWin)(DisplayWin, x, y++);
	    if (!hw->NameLen)
		Act(WriteRow,DisplayWin)(DisplayWin, 9, "(no name)");
	    else
		Act(WriteRow,DisplayWin)(DisplayWin, hw->NameLen, hw->Name);
	}
	if (DisplayWin->Parent)
	    DrawFullWindow2(DisplayWin);
    }
}

static void SelectRowWindow(window CurrWin, uldat newCurY) {
    uldat oldCurY=CurrWin->CurY;

    CurrWin->CurY=newCurY;

    if (oldCurY!=newCurY) {
	DrawLogicWindow2(CurrWin, 0, oldCurY, CurrWin->XWidth + CurrWin->XLogic, oldCurY);
	DrawLogicWindow2(CurrWin, 0, newCurY, CurrWin->XWidth + CurrWin->XLogic, newCurY);
    }
}

static void DisplayGadgetH(msg Msg) {
    display_hw hw;
    uldat i;

    switch (Msg->Event.EventGadget.Code) {
      case COD_D_REMOVE:
	if ((i = DisplayWin->CurY) < DisplayWin->HLogic) {
	    for (hw = All->FirstDisplayHW; hw && i; hw = hw->Next, i--)
		;
	    if (hw && !i)
		Delete(hw);
	}
	break;
      case COD_D_THIS:
	if (All->MouseHW) {
	    for (i = 0, hw = All->FirstDisplayHW; hw; hw = hw->Next, i++) {
		if (hw == All->MouseHW)
		    break;
	    }
	    if (hw)
		SelectRowWindow(DisplayWin, i);
	}
	break;
      default:
	break;
    }
}
	    

static void BuiltinH(msgport MsgPort) {
    msg Msg;
    event_any *Event;
    screen Screen;
    window NewWindow, tempWin;
    row Row;
    udat Code;
    byte /*FontHeight,*/ Flags;
    
    Screen = All->FirstScreen;
    
    while ((Msg = Builtin_MsgPort->FirstMsg)) {
	Remove(Msg);

	Event = &Msg->Event;
	if (Msg->Type == MSG_WINDOW_GADGET) {
	    tempWin = Event->EventGadget.Window;
	    Code = Event->EventGadget.Code;
	    /*0 == Code Chiusura */
	    if (!Code || Code==COD_CANCEL || Code==COD_OK) {
		
		Act(UnMap,tempWin)(tempWin);
		/* no window needs Delete() here */
		
		if (tempWin == ClockWin)
		    Builtin_MsgPort->WakeUp = FALSE;
		
	    } else if (tempWin == OptionWin)
		OptionH(Msg);
	    else if (tempWin == ButtonWin)
		BordersH(Msg);
	    else if (tempWin == DisplaySubWin)
		DisplayGadgetH(Msg);
	    else if (tempWin == ExecuteWin)
		ExecuteGadgetH(&Event->EventGadget);
	}
	else if (Msg->Type==MSG_MENU_ROW) {
	    if (Event->EventMenu.Menu==Builtin_Menu) {
		Code=Event->EventMenu.Code;
		Flags=All->SetUp->Flags;
		switch (Code) {
		  case COD_EXECUTE:
		  case COD_CLOCK_WIN:
		  case COD_OPTION_WIN:
		  case COD_BUTTONS_WIN:
		  case COD_DISPLAY_WIN:
		  case COD_MESSAGES_WIN:
		  case COD_ABOUT_WIN:
		    switch (Code) {
		      case COD_EXECUTE:
					   NewWindow = ExecuteWin; break;
		      case COD_CLOCK_WIN:
			Builtin_MsgPort->WakeUp = TIMER_ALWAYS;
					   NewWindow = ClockWin;   break;
		      case COD_OPTION_WIN:
			UpdateOptionWin(); NewWindow = OptionWin;  break;
		      case COD_BUTTONS_WIN:
			UpdateButtonWin(); NewWindow = ButtonWin;  break;
		      case COD_DISPLAY_WIN:
			UpdateDisplayWin(DisplayWin);
					   NewWindow = DisplayWin; break;
		      case COD_MESSAGES_WIN:
					   NewWindow = MessagesWin;break;
		      case COD_ABOUT_WIN:
					   NewWindow = AboutWin;   break;
		      default:
			break;
		    }
		    if (NewWindow->Parent)
			Act(UnMap,NewWindow)(NewWindow);
		    Act(Map,NewWindow)(NewWindow, (widget)Screen);
		    break;

		  case COD_QUIT:
		    Quit(0);
		    break;
		    
		  case COD_SUSPEND:
		    SuspendHW(TRUE);
		    flushk();
		    
		    kill(getpid(), SIGSTOP);
		    
		    (void)RestartHW(TRUE);
		    break;

		  case COD_DETACH:
		    QuitHW();
		    break;

		  case COD_RELOAD_RC:
		    SendControlMsg(Ext(WM,MsgPort), MSG_CONTROL_RESTART, 0, NULL);
		    break;

#if defined(CONF__MODULES) && !defined(CONF_TERM)
		  case COD_TERM_ON:
		    if (!DlLoad(TermSo))
			break;
		    /* FALLTHROUGH */
#endif
#if defined(CONF__MODULES) || defined(CONF_TERM)
		 case COD_SPAWN:
		    Ext(Term,Open)(NULL, NULL);
		    break;
#endif
#if defined(CONF__MODULES) && !defined(CONF_TERM)
		  case COD_TERM_OFF:
		    DlUnLoad(TermSo);
		    break;
#endif
#if defined(CONF__MODULES) && !defined(CONF_SOCKET)
		  case COD_SOCKET_OFF:
		    DlUnLoad(SocketSo);
		    if (All->FirstDisplayHW)
			break;
		    /* hmm... better to fire it up again */
		    /* FALLTHROUGH */
		  case COD_SOCKET_ON:
		    if (!DlLoad(SocketSo))
			break;
		    break;
#endif
		  default:
		    break;
		}
	    }
	} else if (Msg->Type==MSG_WINDOW_KEY) {
	    if (Msg->Event.EventCommon.Window == WinList) {
		switch (Msg->Event.EventKeyboard.Code) {
		  case TW_Escape:
		    Act(UnMap,WinList)(WinList);
		    break;
		  case TW_Return:
		    SelectWinList();
		    break;
		  default:
		    FallBackKeyAction(WinList, &Msg->Event.EventKeyboard);
		    break;
		}
	    } else if (Msg->Event.EventCommon.Window == ExecuteWin) {
		switch (Msg->Event.EventKeyboard.Code) {
		  case TW_Escape:
		    Act(UnMap,ExecuteWin)(ExecuteWin);
		    break;
		  case TW_Return:
		    ExecuteWinRun();
		    break;
		  case TW_BackSpace:
		    if (ExecuteWin->CurX) {
			ExecuteWin->CurX--;
			UpdateCursor();
			if ((Row = Act(FindRow,ExecuteWin)(ExecuteWin, ExecuteWin->CurY)) && Row->Len) {
			    Row->Len--;
			    DrawLogicWindow2(ExecuteWin, Row->Len, ExecuteWin->CurY,
					   Row->Len + 1, ExecuteWin->CurY);
			}
		    }
		    break;
		  default:
		    if (Msg->Event.EventKeyboard.SeqLen)
			Act(WriteRow,ExecuteWin)(ExecuteWin, Msg->Event.EventKeyboard.SeqLen, Msg->Event.EventKeyboard.AsciiSeq);
		    break;
		}
	    }
	} else if (Msg->Type==MSG_WINDOW_MOUSE) {
	    if (Msg->Event.EventCommon.Window == WinList ||
		Msg->Event.EventCommon.Window == DisplayWin) {
	    
		dat EventMouseX, EventMouseY;
		window CurrWin;
		byte temp;

		EventMouseX = Msg->Event.EventMouse.X, EventMouseY = Msg->Event.EventMouse.Y;
		CurrWin = Msg->Event.EventCommon.Window;
		temp = EventMouseX >= 0 && EventMouseX <= CurrWin->XWidth-2
		    && EventMouseY >= 0 && EventMouseY <= CurrWin->YWidth-2
		    && (uldat)EventMouseY+CurrWin->YLogic < (uldat)CurrWin->HLogic;

		SelectRowWindow(CurrWin, temp ? (uldat)EventMouseY+CurrWin->YLogic : MAXLDAT);
		
		if (CurrWin == WinList &&
		    isRELEASE(Msg->Event.EventMouse.Code)) {
		    if (temp)
			SelectWinList();
		}
	    }
	} else if (Msg->Type==MSG_USER_CONTROL) {
	    switch (Event->EventControl.Code) {
	      case MSG_CONTROL_OPEN:
		{
		    byte **cmd = TokenizeStringVec(Event->EventControl.Len, Event->EventControl.Data);
		    if (cmd) {
			Ext(Term,Open)(cmd[0], cmd);
			FreeStringVec(cmd);
		    } else
			Ext(Term,Open)(NULL, NULL);
		    break;
		}
	      case MSG_CONTROL_QUIT:
		Quit(0);
		break;
	      default:
		break;
	    }
	}
	Delete(Msg);
    }
    if (Builtin_MsgPort->WakeUp)
	Clock_Update();
}

void FullUpdateWinList(window listWin);

void InstallRemoveWinListHook(window listWin) {    
    if (listWin == WinList) {
	if (WinList->Parent && IS_SCREEN(WinList->Parent))
	    Act(InstallHook,WinList)(WinList, FullUpdateWinList,
				     &((screen)WinList->Parent)->FnHookWindow);
	else
	    Act(RemoveHook,WinList)(WinList, FullUpdateWinList, WinList->WhereHook);
    }
}

void UpdateWinList(void) {
    screen Screen = All->FirstScreen;
    widget W;
    
    DeleteList(WinList->USE.R.FirstRow);
    WinList->CurX = WinList->CurY = 0;
    
    WinList->XLogic = WinList->YLogic = 0;
    WinList->XWidth = WinList->MinXWidth;
    WinList->YWidth = WinList->MinYWidth;
    
    for (W = Screen->FirstW; W; W = W->Next) {
	if (W == (widget)WinList || !IS_WINDOW(W) || (((window)W)->Attrib & WINDOW_MENU))
	    continue;
	Row4Menu(WinList, (udat)0, ROW_ACTIVE, ((window)W)->LenTitle, ((window)W)->Title);
    }
}

void FullUpdateWinList(window listWin) {
    if (listWin == WinList && WinList->Parent) {
	ResizeRelWindow(WinList, WinList->MinXWidth - WinList->XWidth, WinList->MinYWidth - WinList->YWidth);
    
	UpdateWinList();
	
	DrawAreaWindow2(WinList);
    }
}


#ifdef CONF_PRINTK
static byte InitMessagesWin(void) {
    MessagesWin = Do(Create,Window)
	(FnWindow, 8, "Messages", NULL,
	 Builtin_Menu, COL(WHITE,BLACK), LINECURSOR,
	 WINDOW_DRAG|WINDOW_RESIZE|WINDOW_X_BAR|WINDOW_Y_BAR|WINDOW_CLOSE,
	 WINFL_CURSOR_ON,
	 60, 20, 200);
    if (MessagesWin) {
	Act(SetColors,MessagesWin)
	    (MessagesWin, 0x1F1, COL(HIGH|GREEN,WHITE), 0, 0, 0, COL(HIGH|WHITE,WHITE),
	     COL(BLACK,WHITE), COL(BLACK,GREEN), COL(HIGH|BLACK,WHITE), COL(HIGH|BLACK,BLACK));
    }
    return !!MessagesWin;
}
#endif

#if 0
static byte InitTestWin(void) {
    group Group;
    
    TestWin = Do(Create,Window)
	(FnWindow, 4, "Test", NULL,
	 Builtin_Menu, COL(WHITE,BLACK), NOCURSOR,
	 WINDOW_DRAG|WINDOW_RESIZE|WINDOW_X_BAR|WINDOW_Y_BAR|WINDOW_CLOSE,
	 0, 40, 20, 0);
    if (TestWin && (Group = Do(Create,Group)(FnGroup, Builtin_MsgPort))) {
	Act(SetColText,TestWin)(TestWin, COL(WHITE,BLUE));
	Act(InsertGadget,Group)
	    (Group, Do(CreateButton,Gadget)
	     (FnGadget, (widget)TestWin, 7, 1, " Test1 ",
	      0, GADGET_TOGGLE,
	      COL(WHITE,BLUE), COL(BLACK,WHITE), COL(HIGH|BLACK,WHITE),
	      2, 2));
	Act(InsertGadget,Group)
	    (Group, Do(CreateButton,Gadget)
	     (FnGadget, (widget)TestWin, 7, 1, " Test2 ",
	      0, GADGET_TOGGLE,
	      COL(WHITE,BLUE), COL(BLACK,WHITE), COL(HIGH|BLACK,WHITE),
	      2, 5));
	Do(CreateButton,Gadget)
	    (FnGadget, (widget)TestWin, 7, 1, " Test3 ",
	     0, GADGET_TOGGLE,
	     COL(WHITE,BLUE), COL(BLACK,WHITE), COL(HIGH|BLACK,WHITE),
	     2, 8);
    }
    return !!TestWin;
}
#endif

static byte InitScreens(void) {
    screen OneScreen, TwoScreen;
#define a HWATTR(COL(HIGH|BLUE,BLUE),'�')
#define b HWATTR(COL(HIGH|BLUE,BLUE),' ')
#define c HWATTR(COL(HIGH|BLUE,BLUE),'�')
    hwattr attr[8] = {
	b,b,a,c,
	c,a,b,b,
    };
#undef a
#undef b
    
    if ((OneScreen = Do(CreateSimple,Screen)(FnScreen, 1, "1", HWATTR(COL(HIGH|BLACK,BLUE),'�'))) &&
	(TwoScreen = Do(Create,Screen)(FnScreen, 1, "2", 4, 2, attr))) {
	
	Act(Own,OneScreen)(OneScreen, Builtin_MsgPort);
	Act(Own,TwoScreen)(TwoScreen, Builtin_MsgPort);
	
	InsertLast(Screen, OneScreen, All);
	InsertLast(Screen, TwoScreen, All);
	    
	return TRUE;
    }
    Error(NOMEMORY);
    printk("twin: InitScreens(): %s\n", ErrStr);
    return FALSE;
}

byte InitBuiltin(void) {
    window Window;
    byte *greeting = "\n"
	"                TWIN             \n"
	"        Text WINdows manager     \n\n"
	"          Version " TWIN_VERSION_STR TWIN_VERSION_EXTRA_STR " by       \n\n"
	"        Massimiliano Ghilardi    \n\n"
	"         <max@Linuz.sns.it>      ";
    uldat grlen = strlen(greeting);
    
    if ((Builtin_MsgPort=Do(Create,MsgPort)
	 (FnMsgPort, 7, "Builtin", (time_t)0, (frac_t)0, 0, BuiltinH)) &&
	
	InitScreens() && /* Do(Create,Screen)() requires Builtin_MsgPort ! */
	
	(All->BuiltinRow = Do(Create,Row)(FnRow, 0, ROW_ACTIVE|ROW_USE_DEFCOL))&&
	
	(Builtin_Menu=Do(Create,Menu)
	 (FnMenu, Builtin_MsgPort, (byte)0x70, (byte)0x20, (byte)0x78, (byte)0x08, (byte)0x74, (byte)0x24, (byte)0)) &&
	Info4Menu(Builtin_Menu, ROW_ACTIVE, (uldat)42, " Hit PAUSE or Mouse Right Button for Menu ", "tttttttttttttttttttttttttttttttttttttttttt") &&
	
	(Window=Win4Menu(Builtin_Menu)) &&
	Row4Menu(Window, COD_CLOCK_WIN,  ROW_ACTIVE, 9, " Clock   ") &&
	Row4Menu(Window, COD_OPTION_WIN, ROW_ACTIVE, 9, " Options ") &&
	Row4Menu(Window, COD_BUTTONS_WIN,ROW_ACTIVE, 9, " Buttons ") &&
	Row4Menu(Window, COD_DISPLAY_WIN,ROW_ACTIVE, 9, " Display ") &&
#ifdef CONF_PRINTK
	Row4Menu(Window,COD_MESSAGES_WIN,ROW_ACTIVE,10, " Messages ") &&
#endif
	Row4Menu(Window, COD_ABOUT_WIN,  ROW_ACTIVE, 9, " About   ") &&
	Item4Menu(Builtin_Menu, Window, TRUE, 3, " � ") &&
	
	(Window=Win4Menu(Builtin_Menu)) &&
#if defined(CONF_TERM) || defined(CONF__MODULES)

	Row4Menu(Window, COD_SPAWN,  ROW_ACTIVE,10, " New Term ") &&
#endif
	Row4Menu(Window, COD_EXECUTE,ROW_ACTIVE,10, " Execute  ") &&
#if defined(CONF_WM_RC) || defined(CONF__MODULES)
	Row4Menu(Window, COD_RELOAD_RC,ROW_ACTIVE,11," Reload RC ") &&
	Row4Menu(Window, (udat)0,    ROW_IGNORE,11, "�����������") &&
#else
	Row4Menu(Window, (udat)0,    ROW_IGNORE,10, "����������") &&
#endif
	Row4Menu(Window, COD_DETACH, ROW_ACTIVE,10, " Detach   ") &&
	Row4Menu(Window, COD_SUSPEND,ROW_ACTIVE,10, " Suspend  ") &&
	Row4Menu(Window, COD_QUIT,   ROW_ACTIVE,10, " Quit     ") &&
	(Builtin_File=Item4Menu(Builtin_Menu, Window, TRUE, 6, " File ")) &&
	
	(Window=Win4Menu(Builtin_Menu)) &&
	Row4Menu(Window, (udat)0, ROW_INACTIVE,11," Undo      ") &&
	Row4Menu(Window, (udat)0, ROW_INACTIVE,11," Redo      ") &&
	Row4Menu(Window, (udat)0, ROW_IGNORE,  11,"�����������") &&
	Row4Menu(Window, (udat)0, ROW_INACTIVE,11," Cut       ") &&
	Row4Menu(Window, (udat)0, ROW_INACTIVE,11," Copy      ") &&
	Row4Menu(Window, (udat)0, ROW_INACTIVE,11," Paste     ") &&
	Row4Menu(Window, (udat)0, ROW_INACTIVE,11," Clear     ") &&
	Row4Menu(Window, (udat)0, ROW_IGNORE,  11,"�����������") &&
	Row4Menu(Window, (udat)0, ROW_INACTIVE,11," Clipboard ") &&
	Item4Menu(Builtin_Menu, Window, TRUE, 6," Edit ") &&
	
#if defined(CONF__MODULES) && !(defined(CONF_TERM) && defined(CONF_SOCKET))
	(Window=Win4Menu(Builtin_Menu)) &&
	(Act(InstallHook,Window)(Window, UpdateMenuRows, &All->FnHookModule), TRUE) &&
	
#if !defined(CONF_TERM)
	Row4Menu(Window, COD_TERM_ON,	ROW_ACTIVE,  20, " Run Twin Term      ") &&
	Row4Menu(Window, COD_TERM_OFF,	ROW_INACTIVE,20, " Stop Twin Term     ") &&
#endif
#if !defined(CONF_SOCKET) && !defined(CONF_TERM)
	Row4Menu(Window, (udat)0,       ROW_IGNORE,  20, "��������������������") &&
#endif	
#if !defined(CONF_SOCKET)
	Row4Menu(Window, COD_SOCKET_ON,	ROW_ACTIVE,  20, " Run Socket Server  ") &&
	Row4Menu(Window, COD_SOCKET_OFF,ROW_INACTIVE,20, " Stop Socket Server ") &&
#endif
	(Builtin_Modules=Item4Menu(Builtin_Menu, Window, TRUE, 9," Modules ")) &&
#endif
	
	Item4MenuCommon(Builtin_Menu) &&
		
	(AboutWin = Do(Create,Window)
	 (FnWindow, 5, "About", "\x7F\x7F\x7F\x7F\x7F", Builtin_Menu, COL(BLACK,WHITE),
	  NOCURSOR, WINDOW_WANT_MOUSE|WINDOW_DRAG|WINDOW_CLOSE, WINFL_USE_DEFCOL,
	  36, 13, 0)) &&

	(ClockWin = Do(Create,Window)
	 (FnWindow, 5, "Clock", NULL, Builtin_Menu, COL(YELLOW,BLUE),
	  NOCURSOR, WINDOW_DRAG|WINDOW_CLOSE, WINFL_USE_DEFCOL,
	  10, 2, 0)) &&

	(OptionWin = Do(Create,Window)
	 (FnWindow, 7, "Options", NULL, Builtin_Menu, COL(HIGH|BLACK,BLACK),
	  NOCURSOR, WINDOW_WANT_MOUSE|WINDOW_DRAG|WINDOW_CLOSE,WINFL_USE_DEFCOL,
	  37, 16, 0)) &&

	(ButtonWin = Do(Create,Window)
	 (FnWindow, 7, "Buttons", NULL, Builtin_Menu, COL(HIGH|BLACK,BLACK),
	  NOCURSOR, WINDOW_WANT_MOUSE|WINDOW_DRAG|WINDOW_CLOSE, WINFL_USE_DEFCOL,
	  37, 19, 0)) &&

	(DisplayWin = Do(Create,Window)
	 (FnWindow, 7, "Display", NULL, Builtin_Menu, COL(HIGH|BLACK,WHITE),
	  NOCURSOR,
	  WINDOW_WANT_MOUSE|WINDOW_AUTO_KEYS|WINDOW_DRAG|WINDOW_RESIZE|WINDOW_CLOSE|WINDOW_X_BAR|WINDOW_Y_BAR,
	  WINFL_SEL_ROWCURR|WINFL_USE_DEFCOL,
	  31, 10, 0)) &&

	(DisplaySubWin = Do(Create,Window)
	 (FnWindow, 0, NULL, NULL, Builtin_Menu, COL(HIGH|BLACK,WHITE),
	  NOCURSOR, (uldat)0, WINFL_USE_DEFCOL,
	  10, MAXDAT, 0)) &&

	(WinList = Do(Create,Window)
	 (FnWindow, 11, "Window List", NULL, Builtin_Menu, COL(WHITE,BLUE),
	  NOCURSOR,
	  WINDOW_WANT_KEYS|WINDOW_WANT_MOUSE|WINDOW_DRAG|WINDOW_CLOSE/*|WINDOW_RESIZE*/
	  |WINDOW_X_BAR|WINDOW_Y_BAR,
	  WINFL_SEL_ROWCURR|WINFL_USE_DEFCOL /*|WINFL_BORDERLESS*/ ,
	  14, 2, 0)) &&

	(ExecuteWin = Do(Create,Window)
	 (FnWindow, 10, "Execute...", NULL, Builtin_Menu, COL(WHITE,BLUE),
	  LINECURSOR, WINDOW_WANT_KEYS|WINDOW_CLOSE|WINDOW_DRAG|WINDOW_X_BAR,
	  WINFL_USE_DEFCOL|WINFL_CURSOR_ON,
	  38, 2, 0)) &&

#ifdef CONF_PRINTK
	InitMessagesWin() &&
#endif
	Act(WriteRow,AboutWin)(AboutWin, grlen, greeting) &&
	
	(ButtonOK_About=Do(CreateEmptyButton,Gadget)(FnGadget, Builtin_MsgPort, 8, 1, COL(BLACK,WHITE))) &&

	(ButtonRemove=Do(CreateEmptyButton,Gadget)(FnGadget, Builtin_MsgPort, 8, 1, COL(BLACK,WHITE))) &&
	(ButtonThis  =Do(CreateEmptyButton,Gadget)(FnGadget, Builtin_MsgPort, 8, 1, COL(BLACK,WHITE))) &&

	Do(Create,Gadget)(FnGadget, Builtin_MsgPort, (widget)OptionWin, 11, 1, "[ ] Shadows",
			  COD_O_SHADOWS, GADGET_USE_DEFCOL,
			  COL(BLACK,WHITE), COL(HIGH|WHITE,GREEN),
			  COL(HIGH|BLACK,WHITE), COL(HIGH|BLACK,BLACK),
			  2, 1, NULL, NULL, NULL, NULL, NULL, NULL, NULL) &&

	Do(Create,Gadget)(FnGadget, Builtin_MsgPort, (widget)OptionWin, 3, 1, "[-]",
			  COD_O_Xn_SHADE, GADGET_USE_DEFCOL,
			  COL(BLACK,WHITE), COL(HIGH|WHITE,GREEN),
			  COL(HIGH|BLACK,WHITE), COL(HIGH|BLACK,BLACK),
			  18, 1, NULL, NULL, NULL, NULL, NULL, NULL, NULL) &&
	
	Do(Create,Gadget)(FnGadget, Builtin_MsgPort, (widget)OptionWin, 3, 1, "[+]",
			  COD_O_Xp_SHADE, GADGET_USE_DEFCOL,
			  COL(BLACK,WHITE), COL(HIGH|WHITE,GREEN),
			  COL(HIGH|BLACK,WHITE), COL(HIGH|BLACK,BLACK),
			  21, 1, NULL, NULL, NULL, NULL, NULL, NULL, NULL) &&

	Do(Create,Gadget)(FnGadget, Builtin_MsgPort, (widget)OptionWin, 3, 1, "[-]",
			  COD_O_Yn_SHADE, GADGET_USE_DEFCOL,
			  COL(BLACK,WHITE), COL(HIGH|WHITE,GREEN),
			  COL(HIGH|BLACK,WHITE), COL(HIGH|BLACK,BLACK),
			  18, 2, NULL, NULL, NULL, NULL, NULL, NULL, NULL) &&
	
	Do(Create,Gadget)(FnGadget, Builtin_MsgPort, (widget)OptionWin, 3, 1, "[+]",
			  COD_O_Yp_SHADE, GADGET_USE_DEFCOL,
			  COL(BLACK,WHITE), COL(HIGH|WHITE,GREEN),
			  COL(HIGH|BLACK,WHITE), COL(HIGH|BLACK,BLACK),
			  21, 2, NULL, NULL, NULL, NULL, NULL, NULL, NULL) &&
	
	Do(Create,Gadget)(FnGadget, Builtin_MsgPort, (widget)OptionWin, 22, 1, "[ ] Always Show Cursor",
			  COD_O_ALWAYSCURSOR, GADGET_USE_DEFCOL,
			  COL(BLACK,WHITE), COL(HIGH|WHITE,GREEN),
			  COL(HIGH|BLACK,WHITE), COL(HIGH|BLACK,BLACK),
			  2, 4, NULL, NULL, NULL, NULL, NULL, NULL, NULL) &&

	Do(Create,Gadget)(FnGadget, Builtin_MsgPort, (widget)OptionWin, 32, 1, "[ ] Enable Blink/High Background",
			  COD_O_BLINK, GADGET_USE_DEFCOL,
			  COL(BLACK,WHITE), COL(HIGH|WHITE,GREEN),
			  COL(HIGH|BLACK,WHITE), COL(HIGH|BLACK,BLACK),
			  2, 6, NULL, NULL, NULL, NULL, NULL, NULL, NULL) &&

	Do(Create,Gadget)(FnGadget, Builtin_MsgPort, (widget)OptionWin, 15, 1, "[ ] Hidden Menu",
			  COD_O_HIDEMENU, GADGET_USE_DEFCOL,
			  COL(BLACK,WHITE), COL(HIGH|WHITE,GREEN),
			  COL(HIGH|BLACK,WHITE), COL(HIGH|BLACK,BLACK),
			  2, 8, NULL, NULL, NULL, NULL, NULL, NULL, NULL) &&

	Do(Create,Gadget)(FnGadget, Builtin_MsgPort, (widget)OptionWin, 25, 1, "[ ] Menu Information Line",
			  COD_O_MENUINFO, GADGET_USE_DEFCOL,
			  COL(BLACK,WHITE), COL(HIGH|WHITE,GREEN),
			  COL(HIGH|BLACK,WHITE), COL(HIGH|BLACK,BLACK),
			  2, 10, NULL, NULL, NULL, NULL, NULL, NULL, NULL) &&

	Do(Create,Gadget)(FnGadget, Builtin_MsgPort, (widget)OptionWin, 27, 1, "[ ] Enable Screen Scrolling",
			  COD_O_EDGESCROLL, GADGET_USE_DEFCOL,
			  COL(BLACK,WHITE), COL(HIGH|WHITE,GREEN),
			  COL(HIGH|BLACK,WHITE), COL(HIGH|BLACK,BLACK),
			  2, 12, NULL, NULL, NULL, NULL, NULL, NULL, NULL) &&

	Do(Create,Gadget)(FnGadget, Builtin_MsgPort, (widget)OptionWin, 15, 1, "[ ] Custom Font", 
			  COD_O_ALTFONT, GADGET_USE_DEFCOL,
			  COL(BLACK,WHITE), COL(HIGH|WHITE,GREEN),
			  COL(HIGH|BLACK,WHITE), COL(HIGH|BLACK,BLACK),
			  2, 14, NULL, NULL, NULL, NULL, NULL, NULL, NULL) &&
	
	Do(Create,Gadget)(FnGadget, Builtin_MsgPort, (widget)ExecuteWin, 19, 1, "[ ] Run in Terminal",
			  COD_E_TTY, GADGET_USE_DEFCOL,
			  COL(HIGH|YELLOW,BLUE), COL(HIGH|WHITE,GREEN),
			  COL(HIGH|BLACK,BLUE), COL(HIGH|BLACK,BLUE),
			  10, 1, NULL, NULL, NULL, NULL, NULL, NULL, NULL)

	)
    {
	Act(SetColors,AboutWin)(AboutWin, 0x1FF, (hwcol)0x7A, (hwcol)0, (hwcol)0, (hwcol)0, (hwcol)0x7F,
				(hwcol)0x70, (hwcol)0x20, (hwcol)0x78, (hwcol)0x08);
	
	Act(SetColors,ClockWin)(ClockWin, 0x1FF, (hwcol)0x3E, (hwcol)0, (hwcol)0, (hwcol)0, (hwcol)0x9F,
				(hwcol)0x1E, (hwcol)0x3E, (hwcol)0x18, (hwcol)0x08);

	Act(SetColors,OptionWin)(OptionWin, 0x1FF, (hwcol)0x7A, (hwcol)0, (hwcol)0, (hwcol)0, (hwcol)0x7F,
				 (hwcol)0x78, (hwcol)0x20, (hwcol)0x78, (hwcol)0x08);

	Act(SetColors,ButtonWin)(ButtonWin, 0x1FF, (hwcol)0x7A, (hwcol)0, (hwcol)0, (hwcol)0, (hwcol)0x7F,
				 (hwcol)0x7F, (hwcol)0x20, (hwcol)0x78, (hwcol)0x08);

	Act(SetColors,WinList)(WinList, 0x1FF,
			       COL(HIGH|YELLOW,CYAN), COL(HIGH|GREEN,HIGH|BLUE), COL(WHITE,HIGH|BLUE),
			       COL(HIGH|WHITE,HIGH|BLUE), COL(HIGH|WHITE,HIGH|BLUE),
			       COL(WHITE,BLUE), COL(HIGH|BLUE,WHITE), COL(HIGH|BLACK,BLUE), COL(HIGH|BLACK,BLACK));
	Act(Configure,WinList)(WinList, 1<<2 | 1<<3, 0, 0, 15, 2, 0, 0);

	Act(SetColors,DisplayWin)(DisplayWin, 0x1FF, (hwcol)0x7A, (hwcol)0x7F, (hwcol)0x79, (hwcol)0xF9, (hwcol)0x7F,
				  (hwcol)0x70, (hwcol)0x20, (hwcol)0x78, (hwcol)0x08);

	Act(SetColors,DisplaySubWin)(DisplaySubWin, 1<<4, 0, 0, 0, 0, COL(HIGH|BLACK,WHITE),
				     0, 0, 0, 0);

	Act(Configure,DisplaySubWin)(DisplaySubWin, 1<<0 | 1<<1, -1, -1, 0, 0, 0, 0);
	Act(Map,DisplaySubWin)(DisplaySubWin, (widget)DisplayWin);
	
	Act(InstallHook,DisplayWin)(DisplayWin, UpdateDisplayWin, &All->FnHookDisplayHW);
	WinList->MapUnMapHook = InstallRemoveWinListHook;
	
	Act(FillButton,ButtonOK_About)(ButtonOK_About, (widget)AboutWin, COD_OK,
				       15, 11, 0, "   OK   ", (byte)0x2F, (byte)0x28);

	Act(FillButton,ButtonRemove)(ButtonRemove, (widget)DisplaySubWin, COD_D_REMOVE,
				     1, 2, 0, " Remove ", (byte)0x2F, (byte)0x28);
	Act(FillButton,ButtonThis)  (ButtonThis,   (widget)DisplaySubWin, COD_D_THIS,
				     1, 5, 0, "  This  ", (byte)0x2F, (byte)0x28);

	OptionWin->CurX = 25; OptionWin->CurY = 1;
	Act(WriteRow,OptionWin)(OptionWin, 10, "  X Shadow");
	OptionWin->CurX = 25; OptionWin->CurY = 2;
	Act(WriteRow,OptionWin)(OptionWin, 10, "  Y Shadow");

	All->BuiltinMenu=Builtin_Menu;

	return TRUE;
    }
    Error(NOMEMORY);
    printk("twin: InitBuiltin(): %s\n", ErrStr);
    return FALSE;
}
