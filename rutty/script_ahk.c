/* script_ahk.c - version 0.15.00

 part of rutty - a modified version of putty
 Copyright 2013-2014, Ernst Dijk
*/


#define ruttyAHK_SIGNATURE 0x87654320
#define ruttyAHK_send     (ruttyAHK_SIGNATURE+0)  //message send to rutty.exe
#define ruttyAHK_enable   (ruttyAHK_SIGNATURE+8)  //enable ahk mode, set ahk window name
#define ruttyAHK_set      (ruttyAHK_SIGNATURE+7)  //enable waitforprompt, set prompt
#define ruttyAHK_transmit (ruttyAHK_SIGNATURE+1)  //data transmitted by rutty to host
#define ruttyAHK_received (ruttyAHK_SIGNATURE+2)  //data received by rutty from host 
#define ruttyAHK_prompt   (ruttyAHK_SIGNATURE+3)  //prompt received by rutty from host


#define ruttyAHK_wn_siz 128
static int ruttyAHK = FALSE;  //not enabled
static char ruttyAHK_windowname[ruttyAHK_wn_siz];

static int ruttyAHK_prompt_c = 0;
static char ruttyAHK_prompt_s[script_cond_size];

/* enable AHK mode
  send empty string to disable 
*/
void script_ahk_enable(COPYDATASTRUCT *cds)
{
  if((cds->cbData <= 0) || (cds->cbData >= ruttyAHK_wn_siz))  //no windowname or to long - stop ruttyAHK
  {
    ruttyAHK = FALSE;
    logevent(NULL, "AHK mode disabled");
    return; 
  }
  
  memcpy(ruttyAHK_windowname, (char *) cds->lpData, cds->cbData);
  ruttyAHK_windowname[cds->cbData]='\0';
  ruttyAHK = TRUE;   
  logevent(NULL, "AHK mode enabled");
  return; 
 }


 /* enable prompt message
  send empty string to disable 
*/
void script_ahk_set(COPYDATASTRUCT *cds)
{
  int siz;
  if(cds->cbData <= 0) //no new prompt 
  {
    ruttyAHK_prompt_c = 0;
    ruttyAHK_prompt_s[0] = '\0';
    logevent(NULL, "notify AHK on prompt disabled");
    return; 
  }

  siz = script_cond_size -1;
  if(cds->cbData < siz) 
    siz = cds->cbData;
  //memcpy(ruttyAHK_prompt_s, (char *) cds->lpData, siz);
  //ruttyAHK_prompt_s[siz]='\0';
  script_cond_set(ruttyAHK_prompt_s, &ruttyAHK_prompt_c, (char *) cds->lpData, cds->cbData);
  logevent(NULL, "notify AHK on prompt enabled");
  return; 
 }

 
/* send rutty data to ahk
*/
void script_ahk_out(int id, char *dat, int siz)
{
   HWND hw;
   COPYDATASTRUCT cds;  

   if(!ruttyAHK)
     return;
     
   //hw = FindWindow(NULL,ruttyAHK_windowname);     
   hw = FindWindowA(NULL,ruttyAHK_windowname);
   if( hw == NULL )
   {
     ruttyAHK = FALSE;  
     logevent(NULL, "AHK target window not found");
     return; 
   }
   
   cds.dwData = (DWORD) id;
   cds.cbData = (DWORD) siz;
   cds.lpData = (LPVOID) dat;
   SendMessage(hw, WM_COPYDATA, (WPARAM)(HWND) MainHwnd, (LPARAM) (LPVOID) &cds);
   return;
}

 
/* send message received from ahk
*/
BOOL script_ahk_send(ScriptData * scriptdata, COPYDATASTRUCT *cds)
 {
    if(scriptdata->runs)
      return FALSE;  /* a script is already running */

    if(cds->cbData<=0)
      return FALSE;  /* no data */
    
    scriptdata->runs = TRUE;
    script_menu(scriptdata);  
    
    scriptdata->nextnextline = scriptdata->filebuffer = smalloc(cds->cbData);
    memcpy(scriptdata->filebuffer, (char *) cds->lpData, cds->cbData);
    scriptdata->filebuffer_end = &scriptdata->filebuffer[cds->cbData];

    logevent(NULL, "sending AHK script to host ...");

    script_getline(scriptdata);
    script_chkline(scriptdata);

    if(scriptdata->enable && !scriptdata->except)  /* start timeout if wait for prompt is enabled */
    {
      scriptdata->send = FALSE;
      scriptdata->latest = schedule_timer(scriptdata->timeout, script_timeout, scriptdata);
    }
    else
    {
      scriptdata->send = TRUE;
      schedule_timer(scriptdata->line_delay, script_sendline, scriptdata);
    }
    return TRUE;
 }


 /* end of file */
