#ifndef _TW_BUILTIN_H
#define _TW_BUILTIN_H

byte InitBuiltin(void);
void FillButtonWin(void);
void UpdateOptionWin(void);

extern window WinList, MessagesWin;
extern msgport Builtin_MsgPort;

#endif /* _TW_BUILTIN_H */