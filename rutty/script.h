/* script.h  version 0.14.08

 part of rutty - a modified version of putty
 Copyright 2013, Ernst Dijk
*/

#ifndef script_h
#define script_h script_h

#define script_line_size 4096
#define script_cond_size 256

struct scriptDATA {
   int line_delay;  /* ms */
   int char_delay;  /* ms */
   char cond_char;  /* condition/remark start character */
   char cond_charR;  /* copy for recording */
   int cond_use;  /* use condition from file */
   int enable;  /* wait for host response */
   int except;  /* except firstline */
   int timeout;  /* sec */
   int crlf;  /* cr/lf translation */
   char waitfor[script_cond_size];
   int waitfor_c;
   char halton[script_cond_size];
   int halton_c;

   char waitfor2[script_cond_size];
   int waitfor2_c;
   int runs;
   int send;

   char * filebuffer;
   char * nextnextline;
   char * filebuffer_end;
   long latest;

   FILE *scriptrecord;

   char * nextline;
   int nextline_c;
   int nextline_cc;
   char remotedata[script_line_size];
   int remotedata_c;
   char localdata[script_line_size];
   int localdata_c;
};
typedef struct scriptDATA ScriptData;


/* script cr/lf translation */
enum {SCRIPT_OFF, SCRIPT_NOLF, SCRIPT_CR, SCRIPT_REC};


/* script mode */
enum {SCRIPT_STOP, SCRIPT_PLAY, SCRIPT_RECORD};


/* script.c */
void script_init(ScriptData * scriptdata, Conf * conf);
BOOL script_sendfile(ScriptData * scriptdata, Filename * script_filename);
void script_close(ScriptData * scriptdata);
void script_remote(ScriptData * scriptdata, const char * data, int len);
void script_local(ScriptData * scriptdata, const char * data, int len);
BOOL script_record(ScriptData * scriptdata, Filename * script_filename);


/* script_win.c */
int prompt_scriptfile(HWND hwnd, char * filename);
void script_fail(char * message);
void script_menu(ScriptData * scriptdata);


#endif

/* end of file */