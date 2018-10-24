/* script_win.c

  part of rutty
  record and replay putty, scripting the easy way
*/

extern void *ldisc;  /* defined in window.c */
extern HWND hwnd;    /* in winstuff.h */


/* sort of copy of prompt_keyfile in winpgen.c
*/
int prompt_scriptfile(HWND hwnd, char * filename)
{
    OPENFILENAME of;
    memset(&of, 0, sizeof(of));
    of.hwndOwner = hwnd;
    of.lpstrFilter = "All Files (*.*)\0*\0\0\0";
    of.lpstrCustomFilter = NULL;
    of.nFilterIndex = 1;
    of.lpstrFile = filename;
    *filename = '\0';
    of.nMaxFile = FILENAME_MAX;
    of.lpstrFileTitle = NULL;
    of.lpstrTitle = "Select Script File ";
    of.Flags = 0;
    return request_file(NULL, &of, FALSE, 1);
}


/* timeout message
*/
void script_fail(char * message)
{
    MessageBox(hwnd,message,appname,MB_OK | MB_ICONEXCLAMATION); //
}


/* change script entry in the menu
*/
void script_menu(ScriptData * scriptdata)
{
  int i;
  for (i = 0; i < lenof(popup_menus); i++)
  {
    if(scriptdata->runs)
    {
      ModifyMenu(popup_menus[i].menu, IDM_SCRIPTSEND, MF_BYCOMMAND | MF_ENABLED, IDM_SCRIPTHALT, "Stop sending script");
    }
    else
    {
      ModifyMenu(popup_menus[i].menu, IDM_SCRIPTHALT, MF_BYCOMMAND | MF_ENABLED, IDM_SCRIPTSEND, "Send script file");
    }
  }
}


/* end of file */
