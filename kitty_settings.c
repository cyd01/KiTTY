
/****
A partir du fichier SETTINGS.C
- prendre le contenu de la fonction void save_open_settings(void *sesskey, Conf *conf) et le mettre dans la fonction ci-dessous void save_open_settings_forced(char *filename, Conf *conf) entre les commantaires // BEGIN COPY/PASTE et // END COPY/PASTE
- remplacer les write_setting_i( par des write_setting_i_forced(
- remplacer les write_setting_s( par des write_setting_s_forced(
- remplacer les write_setting_fontspec( par des write_setting_fontspec_forced(
- remplacer les write_setting_filename( par des write_setting_filename_forced(
- remplacer les wmap( par des wmap_forced(
- remplacer les wprefs( par des wprefs_forced(

LES REMPLACEMENTS DOIVENT SE FAIRE SUR LA SELECTION ENTRE LES COMMENTAIRES UNIQUEMENT !

- prendre le contenu de la fonction void load_open_settings(void *sesskey, Conf *conf) et le mettre dans la fonction ci-dessous void load_open_settings_forced(char *filename, Conf *conf) entre les commantaires // BEGIN COPY/PASTE et // END COPY/PASTE
- remplacer en prenant bien en compte les (
- remplacer les gppi_raw( par des gppi_raw_forced(
- remplacer les gppi( par des gppi_forced(
- remplacer les gpps_raw( par des gpps_raw_forced(
- remplacer les gpps( par des gpps_forced(
- remplacer les gppfile( par des gppfile_forced(
- remplacer les gppfont( par des gppfont_forced(
- remplacer les gppmap( par des gppmap_forced(
- remplacer les gprefs( par des gprefs_forced(
LES REMPLACEMENTS DOIVENT SE FAIRE SUR LA SELECTION ENTRE LES COMMENTAIRES UNIQUEMENT !

SUPPRIMER les sauvegarde/chargement du paramètre folder

Remarque: ce fichier kitty_settings.c est inclus à la toute fin du fichier SETTINGS.C
****/
/* Fonctions prototypes */
void write_setting_i_forced(void *handle, const char *key, int value) ;
void write_setting_s_forced(void *handle, const char *key, const char *value) ;
void write_setting_filename_forced(void *handle, const char *key, Filename *value) ;
void write_setting_fontspec_forced(void *handle, const char *key, FontSpec *font) ;
static void wmap_forced(void *handle, char const *outkey, Conf *conf, int primary,int include_values) ;
static void wprefs_forced(void *sesskey, const char *name, const struct keyvalwhere *mapping, int nvals, Conf *conf, int primary) ;

int read_setting_i_forced(void *handle, const char *key, int defvalue) ;
char *read_setting_s_forced(void *handle, const char *key) ;
Filename *read_setting_filename_forced(void *handle, const char *key) ;

static void gppi_forced(void *handle, const char *name, int def, Conf *conf, int primary) ;
static int gppi_raw_forced(void *handle, const char *name, int def) ;
static void gppfile_forced(void *handle, const char *name, Conf *conf, int primary) ;
static void gpps_forced(void *handle, const char *name, const char *def, Conf *conf, int primary) ;
static char *gpps_raw_forced(void *handle, const char *name, const char *def) ;
static void gppfont_forced(void *handle, const char *name, Conf *conf, int primary)  ;
static int gppmap_forced(void *handle, const char *name, Conf *conf, int primary) ;
static void gprefs_forced(void *sesskey, const char *name, const char *def, const struct keyvalwhere *mapping, int nvals, Conf *conf, int primary) ;

 
/* Fonction principale */
void save_open_settings_forced(char *filename, Conf *conf) {
	FILE *sesskey ;
	if( (sesskey=fopen(filename,"w")) == NULL ) { return ; }
// BEGIN COPY/PASTE
    int i;
    char *p;

    write_setting_i_forced(sesskey, "Present", 1);
    write_setting_s_forced(sesskey, "HostName", conf_get_str(conf, CONF_host));
    write_setting_filename_forced(sesskey, "LogFileName", conf_get_filename(conf, CONF_logfilename));
    write_setting_i_forced(sesskey, "LogType", conf_get_int(conf, CONF_logtype));
    write_setting_i_forced(sesskey, "LogFileClash", conf_get_int(conf, CONF_logxfovr));
    write_setting_i_forced(sesskey, "LogFlush", conf_get_int(conf, CONF_logflush));
    write_setting_i_forced(sesskey, "SSHLogOmitPasswords", conf_get_int(conf, CONF_logomitpass));
    write_setting_i_forced(sesskey, "SSHLogOmitData", conf_get_int(conf, CONF_logomitdata));
    p = "raw";
    {
	const Backend *b = backend_from_proto(conf_get_int(conf, CONF_protocol));
	if (b)
	    p =(char*) b->name;
    }
    write_setting_s_forced(sesskey, "Protocol", p);
    write_setting_i_forced(sesskey, "PortNumber", conf_get_int(conf, CONF_port));
    /* The CloseOnExit numbers are arranged in a different order from
     * the standard FORCE_ON / FORCE_OFF / AUTO. */
    write_setting_i_forced(sesskey, "CloseOnExit", (conf_get_int(conf, CONF_close_on_exit)+2)%3);
    write_setting_i_forced(sesskey, "WarnOnClose", !!conf_get_int(conf, CONF_warn_on_close));
    write_setting_i_forced(sesskey, "PingInterval", conf_get_int(conf, CONF_ping_interval) / 60);	/* minutes */
    write_setting_i_forced(sesskey, "PingIntervalSecs", conf_get_int(conf, CONF_ping_interval) % 60);	/* seconds */
    write_setting_i_forced(sesskey, "TCPNoDelay", conf_get_int(conf, CONF_tcp_nodelay));
    write_setting_i_forced(sesskey, "TCPKeepalives", conf_get_int(conf, CONF_tcp_keepalives));
    write_setting_s_forced(sesskey, "TerminalType", conf_get_str(conf, CONF_termtype));
    write_setting_s_forced(sesskey, "TerminalSpeed", conf_get_str(conf, CONF_termspeed));
    wmap_forced(sesskey, "TerminalModes", conf, CONF_ttymodes, TRUE);

    /* Address family selection */
    write_setting_i_forced(sesskey, "AddressFamily", conf_get_int(conf, CONF_addressfamily));

    /* proxy settings */
    write_setting_s_forced(sesskey, "ProxyExcludeList", conf_get_str(conf, CONF_proxy_exclude_list));
    write_setting_i_forced(sesskey, "ProxyDNS", (conf_get_int(conf, CONF_proxy_dns)+2)%3);
    write_setting_i_forced(sesskey, "ProxyLocalhost", conf_get_int(conf, CONF_even_proxy_localhost));
    write_setting_i_forced(sesskey, "ProxyMethod", conf_get_int(conf, CONF_proxy_type));
    write_setting_s_forced(sesskey, "ProxyHost", conf_get_str(conf, CONF_proxy_host));
    write_setting_i_forced(sesskey, "ProxyPort", conf_get_int(conf, CONF_proxy_port));
    write_setting_s_forced(sesskey, "ProxyUsername", conf_get_str(conf, CONF_proxy_username));
    write_setting_s_forced(sesskey, "ProxyPassword", conf_get_str(conf, CONF_proxy_password));
    write_setting_s_forced(sesskey, "ProxyTelnetCommand", conf_get_str(conf, CONF_proxy_telnet_command));
    write_setting_i_forced(sesskey, "ProxyLogToTerm", conf_get_int(conf, CONF_proxy_log_to_term));
    wmap_forced(sesskey, "Environment", conf, CONF_environmt, TRUE);
    write_setting_s_forced(sesskey, "UserName", conf_get_str(conf, CONF_username));
    write_setting_i_forced(sesskey, "UserNameFromEnvironment", conf_get_int(conf, CONF_username_from_env));
    write_setting_s_forced(sesskey, "LocalUserName", conf_get_str(conf, CONF_localusername));
    write_setting_i_forced(sesskey, "NoPTY", conf_get_int(conf, CONF_nopty));
    write_setting_i_forced(sesskey, "Compression", conf_get_int(conf, CONF_compression));
    write_setting_i_forced(sesskey, "TryAgent", conf_get_int(conf, CONF_tryagent));
    write_setting_i_forced(sesskey, "AgentFwd", conf_get_int(conf, CONF_agentfwd));
    write_setting_i_forced(sesskey, "GssapiFwd", conf_get_int(conf, CONF_gssapifwd));
    write_setting_i_forced(sesskey, "ChangeUsername", conf_get_int(conf, CONF_change_username));
    wprefs_forced(sesskey, "Cipher", ciphernames, CIPHER_MAX, conf, CONF_ssh_cipherlist);
    wprefs_forced(sesskey, "KEX", kexnames, KEX_MAX, conf, CONF_ssh_kexlist);
    wprefs_forced(sesskey, "HostKey", hknames, HK_MAX, conf, CONF_ssh_hklist);
    write_setting_i_forced(sesskey, "RekeyTime", conf_get_int(conf, CONF_ssh_rekey_time));
    write_setting_s_forced(sesskey, "RekeyBytes", conf_get_str(conf, CONF_ssh_rekey_data));
    write_setting_i_forced(sesskey, "SshNoAuth", conf_get_int(conf, CONF_ssh_no_userauth));
    write_setting_i_forced(sesskey, "SshBanner", conf_get_int(conf, CONF_ssh_show_banner));
    write_setting_i_forced(sesskey, "AuthTIS", conf_get_int(conf, CONF_try_tis_auth));
    write_setting_i_forced(sesskey, "AuthKI", conf_get_int(conf, CONF_try_ki_auth));
    write_setting_i_forced(sesskey, "AuthGSSAPI", conf_get_int(conf, CONF_try_gssapi_auth));
#ifndef NO_GSSAPI
    wprefs_forced(sesskey, "GSSLibs", gsslibkeywords, ngsslibs, conf, CONF_ssh_gsslist);
    write_setting_filename_forced(sesskey, "GSSCustom", conf_get_filename(conf, CONF_ssh_gss_custom));
#endif
    write_setting_i_forced(sesskey, "SshNoShell", conf_get_int(conf, CONF_ssh_no_shell));
    write_setting_i_forced(sesskey, "SshProt", conf_get_int(conf, CONF_sshprot));
    write_setting_s_forced(sesskey, "LogHost", conf_get_str(conf, CONF_loghost));
    write_setting_i_forced(sesskey, "SSH2DES", conf_get_int(conf, CONF_ssh2_des_cbc));
    write_setting_filename_forced(sesskey, "PublicKeyFile", conf_get_filename(conf, CONF_keyfile));
    write_setting_s_forced(sesskey, "RemoteCommand", conf_get_str(conf, CONF_remote_cmd));
    write_setting_i_forced(sesskey, "RFCEnviron", conf_get_int(conf, CONF_rfc_environ));
    write_setting_i_forced(sesskey, "PassiveTelnet", conf_get_int(conf, CONF_passive_telnet));
    write_setting_i_forced(sesskey, "BackspaceIsDelete", conf_get_int(conf, CONF_bksp_is_delete));
    write_setting_i_forced(sesskey, "RXVTHomeEnd", conf_get_int(conf, CONF_rxvt_homeend));
    write_setting_i_forced(sesskey, "LinuxFunctionKeys", conf_get_int(conf, CONF_funky_type));
    write_setting_i_forced(sesskey, "NoApplicationKeys", conf_get_int(conf, CONF_no_applic_k));
    write_setting_i_forced(sesskey, "NoApplicationCursors", conf_get_int(conf, CONF_no_applic_c));
    write_setting_i_forced(sesskey, "NoMouseReporting", conf_get_int(conf, CONF_no_mouse_rep));
    write_setting_i_forced(sesskey, "NoRemoteResize", conf_get_int(conf, CONF_no_remote_resize));
    write_setting_i_forced(sesskey, "NoAltScreen", conf_get_int(conf, CONF_no_alt_screen));
    write_setting_i_forced(sesskey, "NoRemoteWinTitle", conf_get_int(conf, CONF_no_remote_wintitle));
    write_setting_i_forced(sesskey, "NoRemoteClearScroll", conf_get_int(conf, CONF_no_remote_clearscroll));
    write_setting_i_forced(sesskey, "RemoteQTitleAction", conf_get_int(conf, CONF_remote_qtitle_action));
    write_setting_i_forced(sesskey, "NoDBackspace", conf_get_int(conf, CONF_no_dbackspace));
    write_setting_i_forced(sesskey, "NoRemoteCharset", conf_get_int(conf, CONF_no_remote_charset));
    write_setting_i_forced(sesskey, "ApplicationCursorKeys", conf_get_int(conf, CONF_app_cursor));
    write_setting_i_forced(sesskey, "ApplicationKeypad", conf_get_int(conf, CONF_app_keypad));
    write_setting_i_forced(sesskey, "NetHackKeypad", conf_get_int(conf, CONF_nethack_keypad));
    write_setting_i_forced(sesskey, "AltF4", conf_get_int(conf, CONF_alt_f4));
    write_setting_i_forced(sesskey, "AltSpace", conf_get_int(conf, CONF_alt_space));
    write_setting_i_forced(sesskey, "AltOnly", conf_get_int(conf, CONF_alt_only));
    write_setting_i_forced(sesskey, "ComposeKey", conf_get_int(conf, CONF_compose_key));
    write_setting_i_forced(sesskey, "CtrlAltKeys", conf_get_int(conf, CONF_ctrlaltkeys));
#ifdef OSX_META_KEY_CONFIG
    write_setting_i_forced(sesskey, "OSXOptionMeta", conf_get_int(conf, CONF_osx_option_meta));
    write_setting_i_forced(sesskey, "OSXCommandMeta", conf_get_int(conf, CONF_osx_command_meta));
#endif
    write_setting_i_forced(sesskey, "TelnetKey", conf_get_int(conf, CONF_telnet_keyboard));
    write_setting_i_forced(sesskey, "TelnetRet", conf_get_int(conf, CONF_telnet_newline));
    write_setting_i_forced(sesskey, "LocalEcho", conf_get_int(conf, CONF_localecho));
    write_setting_i_forced(sesskey, "LocalEdit", conf_get_int(conf, CONF_localedit));
    write_setting_s_forced(sesskey, "Answerback", conf_get_str(conf, CONF_answerback));
    write_setting_i_forced(sesskey, "AlwaysOnTop", conf_get_int(conf, CONF_alwaysontop));
    write_setting_i_forced(sesskey, "FullScreenOnAltEnter", conf_get_int(conf, CONF_fullscreenonaltenter));
    write_setting_i_forced(sesskey, "HideMousePtr", conf_get_int(conf, CONF_hide_mouseptr));
    write_setting_i_forced(sesskey, "SunkenEdge", conf_get_int(conf, CONF_sunken_edge));
    write_setting_i_forced(sesskey, "WindowBorder", conf_get_int(conf, CONF_window_border));
    write_setting_i_forced(sesskey, "CurType", conf_get_int(conf, CONF_cursor_type));
    write_setting_i_forced(sesskey, "BlinkCur", conf_get_int(conf, CONF_blink_cur));
    write_setting_i_forced(sesskey, "Beep", conf_get_int(conf, CONF_beep));
    write_setting_i_forced(sesskey, "BeepInd", conf_get_int(conf, CONF_beep_ind));
    write_setting_filename_forced(sesskey, "BellWaveFile", conf_get_filename(conf, CONF_bell_wavefile));
    write_setting_i_forced(sesskey, "BellOverload", conf_get_int(conf, CONF_bellovl));
    write_setting_i_forced(sesskey, "BellOverloadN", conf_get_int(conf, CONF_bellovl_n));
    write_setting_i_forced(sesskey, "BellOverloadT", conf_get_int(conf, CONF_bellovl_t)
#ifdef PUTTY_UNIX_H
		    * 1000
#endif
		    );
    write_setting_i_forced(sesskey, "BellOverloadS", conf_get_int(conf, CONF_bellovl_s)
#ifdef PUTTY_UNIX_H
		    * 1000
#endif
		    );
    write_setting_i_forced(sesskey, "ScrollbackLines", conf_get_int(conf, CONF_savelines));
    write_setting_i_forced(sesskey, "DECOriginMode", conf_get_int(conf, CONF_dec_om));
    write_setting_i_forced(sesskey, "AutoWrapMode", conf_get_int(conf, CONF_wrap_mode));
    write_setting_i_forced(sesskey, "LFImpliesCR", conf_get_int(conf, CONF_lfhascr));
    write_setting_i_forced(sesskey, "CRImpliesLF", conf_get_int(conf, CONF_crhaslf));
    write_setting_i_forced(sesskey, "DisableArabicShaping", conf_get_int(conf, CONF_arabicshaping));
    write_setting_i_forced(sesskey, "DisableBidi", conf_get_int(conf, CONF_bidi));
    write_setting_i_forced(sesskey, "WinNameAlways", conf_get_int(conf, CONF_win_name_always));
    write_setting_s_forced(sesskey, "WinTitle", conf_get_str(conf, CONF_wintitle));
    write_setting_i_forced(sesskey, "TermWidth", conf_get_int(conf, CONF_width));
    write_setting_i_forced(sesskey, "TermHeight", conf_get_int(conf, CONF_height));
    write_setting_fontspec_forced(sesskey, "Font", conf_get_fontspec(conf, CONF_font));
    write_setting_i_forced(sesskey, "FontQuality", conf_get_int(conf, CONF_font_quality));
    write_setting_i_forced(sesskey, "FontVTMode", conf_get_int(conf, CONF_vtmode));
    write_setting_i_forced(sesskey, "UseSystemColours", conf_get_int(conf, CONF_system_colour));
    write_setting_i_forced(sesskey, "TryPalette", conf_get_int(conf, CONF_try_palette));
    write_setting_i_forced(sesskey, "ANSIColour", conf_get_int(conf, CONF_ansi_colour));
    write_setting_i_forced(sesskey, "Xterm256Colour", conf_get_int(conf, CONF_xterm_256_colour));
    write_setting_i_forced(sesskey, "BoldAsColour", conf_get_int(conf, CONF_bold_style)-1);
#ifdef TUTTYPORT
    write_setting_i_forced(sesskey, "WindowClosable", conf_get_int(conf, CONF_window_closable) );
    write_setting_i_forced(sesskey, "WindowMinimizable", conf_get_int(conf, CONF_window_minimizable) );
    write_setting_i_forced(sesskey, "WindowMaximizable", conf_get_int(conf, CONF_window_maximizable) );
    write_setting_i_forced(sesskey, "WindowHasSysMenu", conf_get_int(conf, CONF_window_has_sysmenu) );
    write_setting_i_forced(sesskey, "DisableBottomButtons", conf_get_int(conf, CONF_bottom_buttons) );
    write_setting_i_forced(sesskey, "BoldAsColourTest", conf_get_int(conf, CONF_bold_colour) );
    write_setting_i_forced(sesskey, "UnderlinedAsColour", conf_get_int(conf, CONF_under_colour) );
    write_setting_i_forced(sesskey, "SelectedAsColour", conf_get_int(conf, CONF_sel_colour) );
    for (i = 0; i < NCFGCOLOURS; i++) {
#else
    for (i = 0; i < 22; i++) {
#endif
	char buf[20], buf2[30];
	sprintf(buf, "Colour%d", i);
	sprintf(buf2, "%d,%d,%d",
		conf_get_int_int(conf, CONF_colours, i*3+0),
		conf_get_int_int(conf, CONF_colours, i*3+1),
		conf_get_int_int(conf, CONF_colours, i*3+2));
	write_setting_s_forced(sesskey, buf, buf2);
    }
    write_setting_i_forced(sesskey, "RawCNP", conf_get_int(conf, CONF_rawcnp));
    write_setting_i_forced(sesskey, "PasteRTF", conf_get_int(conf, CONF_rtf_paste));
    write_setting_i_forced(sesskey, "MouseIsXterm", conf_get_int(conf, CONF_mouse_is_xterm));
    write_setting_i_forced(sesskey, "RectSelect", conf_get_int(conf, CONF_rect_select));
    write_setting_i_forced(sesskey, "MouseOverride", conf_get_int(conf, CONF_mouse_override));
    for (i = 0; i < 256; i += 32) {
	char buf[20], buf2[256];
	int j;
	sprintf(buf, "Wordness%d", i);
	*buf2 = '\0';
	for (j = i; j < i + 32; j++) {
	    sprintf(buf2 + strlen(buf2), "%s%d",
		    (*buf2 ? "," : ""),
		    conf_get_int_int(conf, CONF_wordness, j));
	}
	write_setting_s_forced(sesskey, buf, buf2);
    }
    write_setting_s_forced(sesskey, "LineCodePage", conf_get_str(conf, CONF_line_codepage));
    write_setting_i_forced(sesskey, "CJKAmbigWide", conf_get_int(conf, CONF_cjk_ambig_wide));
    write_setting_i_forced(sesskey, "UTF8Override", conf_get_int(conf, CONF_utf8_override));
    write_setting_s_forced(sesskey, "Printer", conf_get_str(conf, CONF_printer));
    write_setting_i_forced(sesskey, "CapsLockCyr", conf_get_int(conf, CONF_xlat_capslockcyr));
    write_setting_i_forced(sesskey, "ScrollBar", conf_get_int(conf, CONF_scrollbar));
    write_setting_i_forced(sesskey, "ScrollBarFullScreen", conf_get_int(conf, CONF_scrollbar_in_fullscreen));
    write_setting_i_forced(sesskey, "ScrollOnKey", conf_get_int(conf, CONF_scroll_on_key));
    write_setting_i_forced(sesskey, "ScrollOnDisp", conf_get_int(conf, CONF_scroll_on_disp));
    write_setting_i_forced(sesskey, "EraseToScrollback", conf_get_int(conf, CONF_erase_to_scrollback));
    write_setting_i_forced(sesskey, "LockSize", conf_get_int(conf, CONF_resize_action));
    write_setting_i_forced(sesskey, "BCE", conf_get_int(conf, CONF_bce));
    write_setting_i_forced(sesskey, "BlinkText", conf_get_int(conf, CONF_blinktext));
    write_setting_i_forced(sesskey, "X11Forward", conf_get_int(conf, CONF_x11_forward));
    write_setting_s_forced(sesskey, "X11Display", conf_get_str(conf, CONF_x11_display));
    write_setting_i_forced(sesskey, "X11AuthType", conf_get_int(conf, CONF_x11_auth));
    write_setting_filename_forced(sesskey, "X11AuthFile", conf_get_filename(conf, CONF_xauthfile));
    write_setting_i_forced(sesskey, "LocalPortAcceptAll", conf_get_int(conf, CONF_lport_acceptall));
    write_setting_i_forced(sesskey, "RemotePortAcceptAll", conf_get_int(conf, CONF_rport_acceptall));
    wmap_forced(sesskey, "PortForwardings", conf, CONF_portfwd, TRUE);
    write_setting_i_forced(sesskey, "BugIgnore1", 2-conf_get_int(conf, CONF_sshbug_ignore1));
    write_setting_i_forced(sesskey, "BugPlainPW1", 2-conf_get_int(conf, CONF_sshbug_plainpw1));
    write_setting_i_forced(sesskey, "BugRSA1", 2-conf_get_int(conf, CONF_sshbug_rsa1));
    write_setting_i_forced(sesskey, "BugIgnore2", 2-conf_get_int(conf, CONF_sshbug_ignore2));
    write_setting_i_forced(sesskey, "BugHMAC2", 2-conf_get_int(conf, CONF_sshbug_hmac2));
    write_setting_i_forced(sesskey, "BugDeriveKey2", 2-conf_get_int(conf, CONF_sshbug_derivekey2));
    write_setting_i_forced(sesskey, "BugRSAPad2", 2-conf_get_int(conf, CONF_sshbug_rsapad2));
    write_setting_i_forced(sesskey, "BugPKSessID2", 2-conf_get_int(conf, CONF_sshbug_pksessid2));
    write_setting_i_forced(sesskey, "BugRekey2", 2-conf_get_int(conf, CONF_sshbug_rekey2));
    write_setting_i_forced(sesskey, "BugMaxPkt2", 2-conf_get_int(conf, CONF_sshbug_maxpkt2));
    write_setting_i_forced(sesskey, "BugOldGex2", 2-conf_get_int(conf, CONF_sshbug_oldgex2));
    write_setting_i_forced(sesskey, "BugWinadj", 2-conf_get_int(conf, CONF_sshbug_winadj));
    write_setting_i_forced(sesskey, "BugChanReq", 2-conf_get_int(conf, CONF_sshbug_chanreq));
    write_setting_i_forced(sesskey, "StampUtmp", conf_get_int(conf, CONF_stamp_utmp));
    write_setting_i_forced(sesskey, "LoginShell", conf_get_int(conf, CONF_login_shell));
    write_setting_i_forced(sesskey, "ScrollbarOnLeft", conf_get_int(conf, CONF_scrollbar_on_left));
    write_setting_fontspec_forced(sesskey, "BoldFont", conf_get_fontspec(conf, CONF_boldfont));
    write_setting_fontspec_forced(sesskey, "WideFont", conf_get_fontspec(conf, CONF_widefont));
    write_setting_fontspec_forced(sesskey, "WideBoldFont", conf_get_fontspec(conf, CONF_wideboldfont));
    write_setting_i_forced(sesskey, "ShadowBold", conf_get_int(conf, CONF_shadowbold));
    write_setting_i_forced(sesskey, "ShadowBoldOffset", conf_get_int(conf, CONF_shadowboldoffset));
    write_setting_s_forced(sesskey, "SerialLine", conf_get_str(conf, CONF_serline));
    write_setting_i_forced(sesskey, "SerialSpeed", conf_get_int(conf, CONF_serspeed));
    write_setting_i_forced(sesskey, "SerialDataBits", conf_get_int(conf, CONF_serdatabits));
    write_setting_i_forced(sesskey, "SerialStopHalfbits", conf_get_int(conf, CONF_serstopbits));
    write_setting_i_forced(sesskey, "SerialParity", conf_get_int(conf, CONF_serparity));
    write_setting_i_forced(sesskey, "SerialFlowControl", conf_get_int(conf, CONF_serflow));
    write_setting_s_forced(sesskey, "WindowClass", conf_get_str(conf, CONF_winclass));
    write_setting_i_forced(sesskey, "ConnectionSharing", conf_get_int(conf, CONF_ssh_connection_sharing));
    write_setting_i_forced(sesskey, "ConnectionSharingUpstream", conf_get_int(conf, CONF_ssh_connection_sharing_upstream));
    write_setting_i_forced(sesskey, "ConnectionSharingDownstream", conf_get_int(conf, CONF_ssh_connection_sharing_downstream));
    wmap_forced(sesskey, "SSHManualHostKeys", conf, CONF_ssh_manual_hostkeys, FALSE);

/* rutty: */
#ifdef RUTTYPORT
    write_setting_filename_forced(sesskey, "ScriptFileName", conf_get_filename(conf, CONF_script_filename));
    write_setting_i_forced(sesskey, "ScriptMode", (conf_get_int(conf, CONF_script_mode)!=SCRIPT_PLAY)?SCRIPT_STOP:conf_get_int(conf, CONF_script_mode));  /* dont save with record on */
    write_setting_i_forced(sesskey, "ScriptLineDelay", conf_get_int(conf, CONF_script_line_delay));
    write_setting_i_forced(sesskey, "ScriptCharDelay", conf_get_int(conf, CONF_script_char_delay));
    write_setting_s_forced(sesskey, "ScriptCondLine", conf_get_str(conf, CONF_script_cond_line));
    write_setting_i_forced(sesskey, "ScriptCondUse", conf_get_int(conf, CONF_script_cond_use));
    write_setting_i_forced(sesskey, "ScriptCRLF", conf_get_int(conf, CONF_script_crlf));
    write_setting_i_forced(sesskey, "ScriptEnable", conf_get_int(conf, CONF_script_enable));
    write_setting_i_forced(sesskey, "ScriptExcept", conf_get_int(conf, CONF_script_except));
    write_setting_i_forced(sesskey, "ScriptTimeout", conf_get_int(conf, CONF_script_timeout));
    write_setting_s_forced(sesskey, "ScriptWait", conf_get_str(conf, CONF_script_waitfor));
    write_setting_s_forced(sesskey, "ScriptHalt", conf_get_str(conf, CONF_script_halton));
#endif  /* rutty */
#ifdef RECONNECTPORT
    write_setting_i_forced(sesskey, "WakeupReconnect", conf_get_int(conf,CONF_wakeup_reconnect) /*cfg->wakeup_reconnect*/);
    write_setting_i_forced(sesskey, "FailureReconnect", conf_get_int(conf,CONF_failure_reconnect) /*cfg->failure_reconnect*/);
#endif
#if (defined IMAGEPORT) && (!defined FDJ)
	if( get_param("BACKGROUNDIMAGE") ) {
    write_setting_i_forced(sesskey, "BgOpacity", conf_get_int(conf, CONF_bg_opacity) /*cfg->bg_opacity*/);
    write_setting_i_forced(sesskey, "BgSlideshow", conf_get_int(conf, CONF_bg_slideshow) /*cfg->bg_slideshow*/);
    write_setting_i_forced(sesskey, "BgType", conf_get_int(conf, CONF_bg_type) /*cfg->bg_type*/);
    write_setting_filename_forced(sesskey, "BgImageFile", conf_get_filename(conf, CONF_bg_image_filename) /*cfg->bg_image_filename*/);
    write_setting_i_forced(sesskey, "BgImageStyle", conf_get_int(conf, CONF_bg_image_style) /*cfg->bg_image_style*/);
    write_setting_i_forced(sesskey, "BgImageAbsoluteX", conf_get_int(conf, CONF_bg_image_abs_x) /*cfg->bg_image_abs_x*/);
    write_setting_i_forced(sesskey, "BgImageAbsoluteY", conf_get_int(conf, CONF_bg_image_abs_y) /*cfg->bg_image_abs_y*/);
    write_setting_i_forced(sesskey, "BgImagePlacement", conf_get_int(conf, CONF_bg_image_abs_fixed) /*cfg->bg_image_abs_fixed*/);  
	}
#endif
#ifdef URLPORT
    write_setting_i_forced(sesskey, "CopyURLDetection", conf_get_int(conf, CONF_copy_clipbd_url_reg) /*cfg->copy_clipbd_url_reg*/);
#endif
#ifdef HYPERLINKPORT
	/*
	 * HACK: PuttyTray / Nutty
	 * Hyperlink stuff: Save hyperlink settings
	 */
	write_setting_i_forced(sesskey, "HyperlinkUnderline", conf_get_int(conf, CONF_url_underline));
	write_setting_i_forced(sesskey, "HyperlinkUseCtrlClick", conf_get_int(conf, CONF_url_ctrl_click));
	write_setting_i_forced(sesskey, "HyperlinkBrowserUseDefault", conf_get_int(conf, CONF_url_defbrowser));
	write_setting_filename_forced(sesskey, "HyperlinkBrowser", conf_get_filename(conf, CONF_url_browser));
	write_setting_i_forced(sesskey, "HyperlinkRegularExpressionUseDefault", conf_get_int(conf, CONF_url_defregex));
#ifndef NO_HYPERLINK
	if( !strcmp(conf_get_str(conf, CONF_url_regex),"@°@°@NO REGEX--") ) 
		write_setting_s_forced(sesskey, "HyperlinkRegularExpression", urlhack_default_regex ) ;
	else
		write_setting_s_forced(sesskey, "HyperlinkRegularExpression", conf_get_str(conf, CONF_url_regex));
#else
	write_setting_s_forced(sesskey, "HyperlinkRegularExpression", conf_get_str(conf, CONF_url_regex));
#endif
#endif
#ifdef IVPORT
    /* Background */
    for (i = 0; i < 4; i++) {
	static const int CONF_alphas_pc[4][2] = {
	    CONF_alphas_pc_cursor_active,
	    CONF_alphas_pc_cursor_inactive,
	    CONF_alphas_pc_defauly_fg_active,
	    CONF_alphas_pc_defauly_fg_inactive,
	    CONF_alphas_pc_degault_bg_active,
	    CONF_alphas_pc_degault_bg_inactive,
	    CONF_alphas_pc_bg_active,
	    CONF_alphas_pc_bg_inactive
	};
	char buf[16], buf2[16];
	sprintf(buf, "Alpha%d", i);
	sprintf(buf2, "%d,%d",
		conf_get_int(conf, CONF_alphas_pc[i][0]),
		conf_get_int(conf, CONF_alphas_pc[i][1]));
	write_setting_s_forced(sesskey, buf, buf2);
    }
    write_setting_i_forced(sesskey, "BackgroundWallpaper", conf_get_int(conf, CONF_bg_wallpaper));
    write_setting_i_forced(sesskey, "BackgroundEffect", conf_get_int(conf, CONF_bg_effect));
    write_setting_filename_forced(sesskey, "WallpaperFile", conf_get_filename(conf, CONF_wp_file));
    write_setting_i_forced(sesskey, "WallpaperPosition", conf_get_int(conf, CONF_wp_position));
    write_setting_i_forced(sesskey, "WallpaperAlign", conf_get_int(conf, CONF_wp_align));
    write_setting_i_forced(sesskey, "WallpaperVerticalAlign", conf_get_int(conf, CONF_wp_valign));
    write_setting_i_forced(sesskey, "WallpaperMoving", conf_get_int(conf, CONF_wp_moving));
#endif
#ifdef CYGTERMPORT
    //if (do_host)
	write_setting_i_forced(sesskey, "CygtermAltMetabit", conf_get_int(conf, CONF_alt_metabit));
	write_setting_i_forced(sesskey, "CygtermAutoPath", conf_get_int(conf, CONF_cygautopath) );
	write_setting_i_forced(sesskey, "Cygterm64", conf_get_int(conf, CONF_cygterm64));
	write_setting_s_forced(sesskey, "CygtermCommand", conf_get_str(conf, CONF_cygcmd) );
#endif
#ifdef ZMODEMPORT
    write_setting_filename_forced(sesskey, "rzCommand", conf_get_filename(conf, CONF_rzcommand) );
    write_setting_s_forced(sesskey, "rzOptions", conf_get_str(conf, CONF_rzoptions) );
    write_setting_filename_forced(sesskey, "szCommand", conf_get_filename(conf, CONF_szcommand) );
    write_setting_s_forced(sesskey, "szOptions", conf_get_str(conf, CONF_szoptions) );
    write_setting_s_forced(sesskey, "zDownloadDir", conf_get_str(conf, CONF_zdownloaddir) );
#endif
#ifdef PERSOPORT
    if( conf_get_int(conf, CONF_transparencynumber)<-1 ) conf_set_int(conf, CONF_transparencynumber,-1) ;
    if( conf_get_int(conf, CONF_transparencynumber)>255 ) conf_set_int(conf, CONF_transparencynumber,255) ;
    write_setting_i_forced(sesskey, "TransparencyValue", conf_get_int(conf, CONF_transparencynumber) /*(unsigned int) cfg->transparencynumber*/) ;
    write_setting_i_forced(sesskey, "SendToTray", conf_get_int(conf, CONF_sendtotray) /*cfg->sendtotray*/);
    write_setting_i_forced(sesskey, "Maximize", conf_get_int(conf, CONF_maximize) /*cfg->maximize*/);
    write_setting_i_forced(sesskey, "Fullscreen", conf_get_int(conf, CONF_fullscreen) /*cfg->fullscreen*/);
    write_setting_i_forced(sesskey, "SaveOnExit", conf_get_int(conf, CONF_saveonexit) /*cfg->saveonexit*/);
    write_setting_i_forced(sesskey, "Icone", conf_get_int(conf, CONF_icone) /*cfg->icone*/);
    //write_setting_s_forced(sesskey, "IconeFile", conf_get_str(conf, CONF_iconefile) /*cfg->iconefile*/);
    write_setting_filename_forced(sesskey, "IconeFile", conf_get_filename(conf, CONF_iconefile) /*cfg->iconefile*/);
    write_setting_s_forced(sesskey, "SFTPConnect", conf_get_str(conf, CONF_sftpconnect) );
    Filename * fn = filename_from_str( "" ) ;
    conf_set_filename(conf,CONF_scriptfile,fn);
    write_setting_filename_forced(sesskey, "Scriptfile", conf_get_filename(conf, CONF_scriptfile) /*cfg->scriptfile*/);  // C'est le contenu uniquement qui est important a sauvegarder
    filename_free(fn);
    write_setting_s_forced(sesskey, "ScriptfileContent", conf_get_str(conf, CONF_scriptfilecontent) );
    write_setting_s_forced(sesskey, "AntiIdle", conf_get_str(conf, CONF_antiidle) /*cfg->antiidle*/);
    write_setting_s_forced(sesskey, "LogTimestamp", conf_get_str(conf, CONF_logtimestamp) /*cfg->logtimestamp*/);
    write_setting_s_forced(sesskey, "Autocommand", conf_get_str(conf, CONF_autocommand) /*cfg->autocommand*/);
    write_setting_s_forced(sesskey, "AutocommandOut", conf_get_str(conf, CONF_autocommandout) /*cfg->autocommandout*/);
    write_setting_s_forced(sesskey, "Folder", conf_get_str(conf, CONF_folder) /*cfg->folder*/) ;
    write_setting_i_forced(sesskey, "LogTimeRotation", conf_get_int(conf, CONF_logtimerotation) /*cfg->logtimerotation*/) ;
    write_setting_i_forced(sesskey, "TermXPos", conf_get_int(conf, CONF_xpos) /*cfg->xpos*/) ;
    write_setting_i_forced(sesskey, "TermYPos", conf_get_int(conf, CONF_ypos) /*cfg->ypos*/) ;
    write_setting_i_forced(sesskey, "WindowState", conf_get_int(conf, CONF_windowstate) /*cfg->windowstate*/) ;
    write_setting_i_forced(sesskey, "SaveWindowPos", conf_get_int(conf, CONF_save_windowpos) /*cfg->save_windowpos*/); /* BKG */
    write_setting_i_forced(sesskey, "ForegroundOnBell", conf_get_int(conf, CONF_foreground_on_bell) /*cfg->foreground_on_bell*/);

    if( (strlen(conf_get_str(conf, CONF_host))+strlen(conf_get_str(conf, CONF_termtype))) < 1000 ) { 
	sprintf( PassKey, "%s%sKiTTY", conf_get_str(conf, CONF_host), conf_get_str(conf, CONF_termtype) ) ;
    } else { 
	strcpy( PassKey, "" ) ;
    }
    char pst[4096] ;
    strcpy( pst, conf_get_str(conf, CONF_password ) );
    MASKPASS(pst);
    cryptstring( pst, PassKey ) ;
    write_setting_s_forced(sesskey, "Password", pst );
    memset(pst,0,strlen(pst));
    
    write_setting_i_forced(sesskey, "CtrlTabSwitch", conf_get_int(conf, CONF_ctrl_tab_switch));
    write_setting_s_forced(sesskey, "Comment", conf_get_str(conf, CONF_comment) );
    write_setting_i_forced(sesskey, "ACSinUTF", conf_get_int(conf, CONF_acs_in_utf));
    write_setting_i_forced(sesskey, "SCPAutoPwd", conf_get_int(conf, CONF_scp_auto_pwd));
#endif
#ifdef PORTKNOCKINGPORT
	write_setting_s_forced(sesskey, "PortKnocking", conf_get_str(conf, CONF_portknockingoptions) );
#endif
#ifdef DISABLEALTGRPORT
	write_setting_i_forced(sesskey, "DisableAltGr", conf_get_int(conf, CONF_disablealtgr));
#endif

// END COPY/PASTE
	fclose(sesskey) ;
}



void load_open_settings_forced(char *filename, Conf *conf) {
	FILE *sesskey ;
	if( (sesskey=fopen(filename,"r")) == NULL ) { 
		char buffer[1024] ;
		sprintf(buffer,"File %s not found !",filename);
		MessageBox(NULL, buffer, "Error", MB_OK|MB_ICONERROR) ; return ; 
		}
// BEGIN COPY/PASTE
    int i;
    char *prot;

		
	Conf * confDef ;
	confDef = conf_new() ;
	do_defaults( "Default Settings" , confDef);
		
#ifdef PERSOPORT
    /*
     * HACK: PuTTY-url
     * Set font quality to cleartype on Windows Vista and above
     */
    OSVERSIONINFO versioninfo;
    versioninfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    GetVersionEx(&versioninfo);
#endif

    conf_set_int(conf, CONF_ssh_subsys, 0);   /* FIXME: load this properly */
    conf_set_str(conf, CONF_remote_cmd, "");
    conf_set_str(conf, CONF_remote_cmd2, "");
    conf_set_str(conf, CONF_ssh_nc_host, "");
    gpps_forced(sesskey, "HostName", "", conf, CONF_host);
    gppfile_forced(sesskey, "LogFileName", conf, CONF_logfilename);
    gppi_forced(sesskey, "LogType", 0, conf, CONF_logtype);		//
    gppi_forced(sesskey, "LogFileClash", LGXF_ASK, conf, CONF_logxfovr);
    gppi_forced(sesskey, "LogFlush", 1, conf, CONF_logflush);
    gppi_forced(sesskey, "SSHLogOmitPasswords", 1, conf, CONF_logomitpass);
    gppi_forced(sesskey, "SSHLogOmitData", 0, conf, CONF_logomitdata);
    prot = gpps_raw_forced(sesskey, "Protocol", "default");
    conf_set_int(conf, CONF_protocol, default_protocol);
    conf_set_int(conf, CONF_port, default_port);
    {
	const Backend *b = backend_from_name(prot);
	if (b) {
	    conf_set_int(conf, CONF_protocol, b->protocol);
	    gppi_forced(sesskey, "PortNumber", default_port, conf, CONF_port);
	}
    }
    sfree(prot);
    
    /* Address family selection */
    gppi_forced(sesskey, "AddressFamily", ADDRTYPE_UNSPEC, conf, CONF_addressfamily);

    /* The CloseOnExit numbers are arranged in a different order from
     * the standard FORCE_ON / FORCE_OFF / AUTO. */
    i = gppi_raw_forced(sesskey, "CloseOnExit", 1); conf_set_int(conf, CONF_close_on_exit, (i+1)%3);
    gppi_forced(sesskey, "WarnOnClose", 1, conf, CONF_warn_on_close);
    {
	/* This is two values for backward compatibility with 0.50/0.51 */
	int pingmin, pingsec;
	pingmin = gppi_raw_forced(sesskey, "PingInterval", conf_get_int(confDef, CONF_ping_interval) / 60 );
	pingsec = gppi_raw_forced(sesskey, "PingIntervalSecs", conf_get_int(confDef, CONF_ping_interval) % 60 );
	conf_set_int(conf, CONF_ping_interval, pingmin * 60 + pingsec);
    }
    gppi_forced(sesskey, "TCPNoDelay", conf_get_int(confDef, CONF_tcp_nodelay), conf, CONF_tcp_nodelay);
    gppi_forced(sesskey, "TCPKeepalives", conf_get_int(confDef, CONF_tcp_keepalives), conf, CONF_tcp_keepalives);
    gpps_forced(sesskey, "TerminalType", "xterm", conf, CONF_termtype);
    gpps_forced(sesskey, "TerminalSpeed", "38400,38400", conf, CONF_termspeed);
    if (gppmap_forced(sesskey, "TerminalModes", conf, CONF_ttymodes)) {
	/*
	 * Backwards compatibility with old saved settings.
	 *
	 * From the invention of this setting through 0.67, the set of
	 * terminal modes was fixed, and absence of a mode from this
	 * setting meant the user had explicitly removed it from the
	 * UI and we shouldn't send it.
	 *
	 * In 0.68, the IUTF8 mode was added, and in handling old
	 * settings we inadvertently removed the ability to not send
	 * a mode. Any mode not mentioned was treated as if it was
	 * set to 'auto' (A).
	 *
	 * After 0.68, we added explicit notation to the setting format
	 * when the user removes a known terminal mode from the list.
	 *
	 * So: if any of the modes from the original set is missing, we
	 * assume this was an intentional removal by the user and add
	 * an explicit removal ('N'); but if IUTF8 (or any other mode
	 * added after 0.67) is missing, we assume that its absence is
	 * due to the setting being old rather than intentional, and
	 * add it with its default setting.
	 *
	 * (This does mean that if a 0.68 user explicitly removed IUTF8,
	 * we add it back; but removing IUTF8 had no effect in 0.68, so
	 * we're preserving behaviour, which is the best we can do.)
	 */
	for (i = 0; ttymodes[i]; i++) {
	    if (!conf_get_str_str_opt(conf, CONF_ttymodes, ttymodes[i])) {
		/* Mode not mentioned in setting. */
		const char *def;
		if (!strcmp(ttymodes[i], "IUTF8")) {
		    /* Any new modes we add in future should be treated
		     * this way too. */
		    def = "A";  /* same as new-setting default below */
		} else {
		    /* One of the original modes. Absence is probably
		     * deliberate. */
		    def = "N";  /* don't send */
		}
		conf_set_str_str(conf, CONF_ttymodes, ttymodes[i], def);
	    }
	}
    } else {
	/* This hardcodes a big set of defaults in any new saved
	 * sessions. Let's hope we don't change our mind. */
	for (i = 0; ttymodes[i]; i++)
	    conf_set_str_str(conf, CONF_ttymodes, ttymodes[i], "A");
    }

    /* proxy settings */
    gpps_forced(sesskey, "ProxyExcludeList", "", conf, CONF_proxy_exclude_list);
    i = gppi_raw_forced(sesskey, "ProxyDNS", 1); conf_set_int(conf, CONF_proxy_dns, (i+1)%3);
    gppi_forced(sesskey, "ProxyLocalhost", 0, conf, CONF_even_proxy_localhost);
    gppi_forced(sesskey, "ProxyMethod", -1, conf, CONF_proxy_type);
    if (conf_get_int(conf, CONF_proxy_type) == -1) {
        int i;
        i = gppi_raw_forced(sesskey, "ProxyType", 0);
        if (i == 0)
            conf_set_int(conf, CONF_proxy_type, PROXY_NONE);
        else if (i == 1)
            conf_set_int(conf, CONF_proxy_type, PROXY_HTTP);
        else if (i == 3)
            conf_set_int(conf, CONF_proxy_type, PROXY_TELNET);
        else if (i == 4)
            conf_set_int(conf, CONF_proxy_type, PROXY_CMD);
        else {
            i = gppi_raw_forced(sesskey, "ProxySOCKSVersion", 5);
            if (i == 5)
                conf_set_int(conf, CONF_proxy_type, PROXY_SOCKS5);
            else
                conf_set_int(conf, CONF_proxy_type, PROXY_SOCKS4);
        }
    }
    gpps_forced(sesskey, "ProxyHost", "proxy", conf, CONF_proxy_host);
    gppi_forced(sesskey, "ProxyPort", 80, conf, CONF_proxy_port);
    gpps_forced(sesskey, "ProxyUsername", "", conf, CONF_proxy_username);
    gpps_forced(sesskey, "ProxyPassword", "", conf, CONF_proxy_password);
    gpps_forced(sesskey, "ProxyTelnetCommand", "connect %host %port\\n",
	 conf, CONF_proxy_telnet_command);
    gppi_forced(sesskey, "ProxyLogToTerm", FORCE_OFF, conf, CONF_proxy_log_to_term);
    gppmap_forced(sesskey, "Environment", conf, CONF_environmt);
    gpps_forced(sesskey, "UserName", "", conf, CONF_username);
    gppi_forced(sesskey, "UserNameFromEnvironment", 0, conf, CONF_username_from_env);
    gpps_forced(sesskey, "LocalUserName", "", conf, CONF_localusername);
    gppi_forced(sesskey, "NoPTY", 0, conf, CONF_nopty);
    gppi_forced(sesskey, "Compression", 0, conf, CONF_compression);
    gppi_forced(sesskey, "TryAgent", 1, conf, CONF_tryagent);
    gppi_forced(sesskey, "AgentFwd", conf_get_int(confDef,CONF_agentfwd), conf, CONF_agentfwd);
    gppi_forced(sesskey, "ChangeUsername", 0, conf, CONF_change_username);
    gppi_forced(sesskey, "GssapiFwd", 0, conf, CONF_gssapifwd);
    gprefs_forced(sesskey, "Cipher", "\0",
	   ciphernames, CIPHER_MAX, conf, CONF_ssh_cipherlist);
    {
	/* Backward-compatibility: before 0.58 (when the "KEX"
	 * preference was first added), we had an option to
	 * disable gex under the "bugs" panel after one report of
	 * a server which offered it then choked, but we never got
	 * a server version string or any other reports. */
	const char *default_kexes,
	           *normal_default = "ecdh,dh-gex-sha1,dh-group14-sha1,rsa,"
		       "WARN,dh-group1-sha1",
		   *bugdhgex2_default = "ecdh,dh-group14-sha1,rsa,"
		       "WARN,dh-group1-sha1,dh-gex-sha1";
	char *raw;
	i = 2 - gppi_raw_forced(sesskey, "BugDHGEx2", 0);
	if (i == FORCE_ON)
            default_kexes = bugdhgex2_default;
	else
            default_kexes = normal_default;
	/* Migration: after 0.67 we decided we didn't like
	 * dh-group1-sha1. If it looks like the user never changed
	 * the defaults, quietly upgrade their settings to demote it.
	 * (If they did, they're on their own.) */
	raw = gpps_raw_forced(sesskey, "KEX", default_kexes);
	assert(raw != NULL);
	/* Lack of 'ecdh' tells us this was saved by 0.58-0.67
	 * inclusive. If it was saved by a later version, we need
	 * to leave it alone. */
	if (strcmp(raw, "dh-group14-sha1,dh-group1-sha1,rsa,"
		   "WARN,dh-gex-sha1") == 0) {
	    /* Previously migrated from BugDHGEx2. */
	    sfree(raw);
	    raw = dupstr(bugdhgex2_default);
	} else if (strcmp(raw, "dh-gex-sha1,dh-group14-sha1,"
			  "dh-group1-sha1,rsa,WARN") == 0) {
	    /* Untouched old default setting. */
	    sfree(raw);
	    raw = dupstr(normal_default);
	}
	gprefs_from_str(raw, kexnames, KEX_MAX, conf, CONF_ssh_kexlist);
	sfree(raw);
    }
    gprefs_forced(sesskey, "HostKey", "ed25519,ecdsa,rsa,dsa,WARN",
           hknames, HK_MAX, conf, CONF_ssh_hklist);
    gppi_forced(sesskey, "RekeyTime", 60, conf, CONF_ssh_rekey_time);
    gpps_forced(sesskey, "RekeyBytes", "1G", conf, CONF_ssh_rekey_data);
    {
    /* SSH-2 only by default */
	int sshprot = gppi_raw_forced(sesskey, "SshProt", 3);
	/* Old sessions may contain the values corresponding to the fallbacks
	 * we used to allow; migrate them */
	if (sshprot == 1)      sshprot = 0; /* => "SSH-1 only" */
	else if (sshprot == 2) sshprot = 3; /* => "SSH-2 only" */
	conf_set_int(conf, CONF_sshprot, sshprot);
    }
    gpps_forced(sesskey, "LogHost", "", conf, CONF_loghost);
    gppi_forced(sesskey, "SSH2DES", 0, conf, CONF_ssh2_des_cbc);
    gppi_forced(sesskey, "SshNoAuth", 0, conf, CONF_ssh_no_userauth);
    gppi_forced(sesskey, "SshBanner", 1, conf, CONF_ssh_show_banner);
    gppi_forced(sesskey, "AuthTIS", 0, conf, CONF_try_tis_auth);
    gppi_forced(sesskey, "AuthKI", 1, conf, CONF_try_ki_auth);
    gppi_forced(sesskey, "AuthGSSAPI", 1, conf, CONF_try_gssapi_auth);
#ifndef NO_GSSAPI
    gprefs_forced(sesskey, "GSSLibs", "\0",
	   gsslibkeywords, ngsslibs, conf, CONF_ssh_gsslist);
    gppfile_forced(sesskey, "GSSCustom", conf, CONF_ssh_gss_custom);
#endif
    gppi_forced(sesskey, "SshNoShell", 0, conf, CONF_ssh_no_shell);
    gppfile_forced(sesskey, "PublicKeyFile", conf, CONF_keyfile);
    gpps_forced(sesskey, "RemoteCommand", "", conf, CONF_remote_cmd);
    gppi_forced(sesskey, "RFCEnviron", 0, conf, CONF_rfc_environ);
    gppi_forced(sesskey, "PassiveTelnet", 0, conf, CONF_passive_telnet);
    gppi_forced(sesskey, "BackspaceIsDelete", 1, conf, CONF_bksp_is_delete);
    gppi_forced(sesskey, "RXVTHomeEnd", 0, conf, CONF_rxvt_homeend);
    gppi_forced(sesskey, "LinuxFunctionKeys", 0, conf, CONF_funky_type);
    gppi_forced(sesskey, "NoApplicationKeys", 0, conf, CONF_no_applic_k);
    gppi_forced(sesskey, "NoApplicationCursors", 0, conf, CONF_no_applic_c);
    gppi_forced(sesskey, "NoMouseReporting", 0, conf, CONF_no_mouse_rep);
    gppi_forced(sesskey, "NoRemoteResize", 0, conf, CONF_no_remote_resize);
    gppi_forced(sesskey, "NoAltScreen", 0, conf, CONF_no_alt_screen);
    gppi_forced(sesskey, "NoRemoteWinTitle", 0, conf, CONF_no_remote_wintitle);
    gppi_forced(sesskey, "NoRemoteClearScroll", 0, conf, CONF_no_remote_clearscroll);
    {
	/* Backward compatibility */
	int no_remote_qtitle = gppi_raw_forced(sesskey, "NoRemoteQTitle", 1);
	/* We deliberately interpret the old setting of "no response" as
	 * "empty string". This changes the behaviour, but hopefully for
	 * the better; the user can always recover the old behaviour. */
	gppi_forced(sesskey, "RemoteQTitleAction",
	     no_remote_qtitle ? TITLE_EMPTY : TITLE_REAL,
	     conf, CONF_remote_qtitle_action);
    }
    gppi_forced(sesskey, "NoDBackspace", 0, conf, CONF_no_dbackspace);
    gppi_forced(sesskey, "NoRemoteCharset", 0, conf, CONF_no_remote_charset);
    gppi_forced(sesskey, "ApplicationCursorKeys", 0, conf, CONF_app_cursor);
    gppi_forced(sesskey, "ApplicationKeypad", 0, conf, CONF_app_keypad);
    gppi_forced(sesskey, "NetHackKeypad", 0, conf, CONF_nethack_keypad);
    gppi_forced(sesskey, "AltF4", 1, conf, CONF_alt_f4);
    gppi_forced(sesskey, "AltSpace", 0, conf, CONF_alt_space);
    gppi_forced(sesskey, "AltOnly", 0, conf, CONF_alt_only);
    gppi_forced(sesskey, "ComposeKey", 0, conf, CONF_compose_key);
    gppi_forced(sesskey, "CtrlAltKeys", 1, conf, CONF_ctrlaltkeys);
#ifdef OSX_META_KEY_CONFIG
    gppi_forced(sesskey, "OSXOptionMeta", 1, conf, CONF_osx_option_meta);
    gppi_forced(sesskey, "OSXCommandMeta", 0, conf, CONF_osx_command_meta);
#endif
    gppi_forced(sesskey, "TelnetKey", 0, conf, CONF_telnet_keyboard);
    gppi_forced(sesskey, "TelnetRet", 1, conf, CONF_telnet_newline);
    gppi_forced(sesskey, "LocalEcho", AUTO, conf, CONF_localecho);
    gppi_forced(sesskey, "LocalEdit", AUTO, conf, CONF_localedit);
#if (defined PERSOPORT) && (!defined FDJ)
    gpps_forced(sesskey, "Answerback", "KiTTY", conf, CONF_answerback);
#else
    gpps_forced(sesskey, "Answerback", "PuTTY", conf, CONF_answerback);
#endif
    gppi_forced(sesskey, "AlwaysOnTop", 0, conf, CONF_alwaysontop);
    gppi_forced(sesskey, "FullScreenOnAltEnter", 0, conf, CONF_fullscreenonaltenter);
    gppi_forced(sesskey, "HideMousePtr", 0, conf, CONF_hide_mouseptr);
    gppi_forced(sesskey, "SunkenEdge", 0, conf, CONF_sunken_edge);
    gppi_forced(sesskey, "WindowBorder", 1, conf, CONF_window_border);
    gppi_forced(sesskey, "CurType", 0, conf, CONF_cursor_type);
    gppi_forced(sesskey, "BlinkCur", 0, conf, CONF_blink_cur);
    /* pedantic compiler tells me I can't use conf, CONF_beep as an int * :-) */
    gppi_forced(sesskey, "Beep", 1, conf, CONF_beep);
    gppi_forced(sesskey, "BeepInd", 0, conf, CONF_beep_ind);
    gppfile_forced(sesskey, "BellWaveFile", conf, CONF_bell_wavefile);
    gppi_forced(sesskey, "BellOverload", 1, conf, CONF_bellovl);
    gppi_forced(sesskey, "BellOverloadN", 5, conf, CONF_bellovl_n);
    i = gppi_raw_forced(sesskey, "BellOverloadT", 2*TICKSPERSEC
#ifdef PUTTY_UNIX_H
				   *1000
#endif
				   );
    conf_set_int(conf, CONF_bellovl_t, i
#ifdef PUTTY_UNIX_H
		 / 1000
#endif
		 );
    i = gppi_raw_forced(sesskey, "BellOverloadS", 5*TICKSPERSEC
#ifdef PUTTY_UNIX_H
				   *1000
#endif
				   );
    conf_set_int(conf, CONF_bellovl_s, i
#ifdef PUTTY_UNIX_H
		 / 1000
#endif
		 );
#ifdef HYPERLINKPORT
    gppi_forced(sesskey, "ScrollbackLines", 10000, conf, CONF_savelines);
#else
    gppi_forced(sesskey, "ScrollbackLines", 2000, conf, CONF_savelines);
#endif
    gppi_forced(sesskey, "DECOriginMode", 0, conf, CONF_dec_om);
    gppi_forced(sesskey, "AutoWrapMode", 1, conf, CONF_wrap_mode);
    gppi_forced(sesskey, "LFImpliesCR", 0, conf, CONF_lfhascr);
    gppi_forced(sesskey, "CRImpliesLF", 0, conf, CONF_crhaslf);
    gppi_forced(sesskey, "DisableArabicShaping", 0, conf, CONF_arabicshaping);
    gppi_forced(sesskey, "DisableBidi", 0, conf, CONF_bidi);
    gppi_forced(sesskey, "WinNameAlways", 1, conf, CONF_win_name_always);
    gpps_forced(sesskey, "WinTitle", "", conf, CONF_wintitle);
    gppi_forced(sesskey, "TermWidth", 80, conf, CONF_width);
    gppi_forced(sesskey, "TermHeight", 24, conf, CONF_height);
    gppfont_forced(sesskey, "Font", conf, CONF_font);

#ifdef PERSOPORT
    /*
     * HACK: PuTTY-url
     * Set font quality to cleartype on Windows Vista and higher
     */
    if (versioninfo.dwMajorVersion >= 6) {
        gppi_forced(sesskey, "FontQuality", FQ_CLEARTYPE, conf, CONF_font_quality);
    } else {
        gppi_forced(sesskey, "FontQuality", FQ_DEFAULT, conf, CONF_font_quality);
    }
#else
    gppi_forced(sesskey, "FontQuality", FQ_DEFAULT, conf, CONF_font_quality);
#endif
    gppi_forced(sesskey, "FontVTMode", VT_UNICODE, conf, CONF_vtmode);
    gppi_forced(sesskey, "UseSystemColours", 0, conf, CONF_system_colour);
    gppi_forced(sesskey, "TryPalette", 0, conf, CONF_try_palette);
    gppi_forced(sesskey, "ANSIColour", 1, conf, CONF_ansi_colour);
    gppi_forced(sesskey, "Xterm256Colour", 1, conf, CONF_xterm_256_colour);
    i = gppi_raw_forced(sesskey, "BoldAsColour", 1); conf_set_int(conf, CONF_bold_style, i+1);
#ifdef TUTTYPORT
    gppi_forced(sesskey, "WindowClosable", 1, conf, CONF_window_closable);
    gppi_forced(sesskey, "WindowMinimizable", 1, conf, CONF_window_minimizable);
    gppi_forced(sesskey, "WindowMaximizable", 1, conf, CONF_window_maximizable);
    gppi_forced(sesskey, "WindowHasSysMenu", 1, conf, CONF_window_has_sysmenu);
    gppi_forced(sesskey, "DisableBottomButtons", 1, conf, CONF_bottom_buttons);
    gppi_forced(sesskey, "BoldAsColourTest", 1, conf, CONF_bold_colour);
    gppi_forced(sesskey, "UnderlinedAsColour", 0, conf, CONF_under_colour);
    gppi_forced(sesskey, "SelectedAsColour", 0, conf, CONF_sel_colour);
    for (i = 0; i < NCFGCOLOURS; i++) {
	static const char *const defaults[NCFGCOLOURS] = {
	    "187,187,187", "255,255,255", "0,0,0", "85,85,85", "0,0,0",
	    "0,255,0", "0,0,0", "85,85,85", "187,0,0", "255,85,85",
	    "0,187,0", "85,255,85", "187,187,0", "255,255,85", "0,0,187",
	    "85,85,255", "187,0,187", "255,85,255", "0,187,187",
	    "85,255,255", "187,187,187", "255,255,255", "187,187,187",
	    "0,0,0", "0,0,0", "187,0,0", "0,187,0", "187,187,0", "0,0,187",
	    "187,0,187", "0,187,187", "187,187,187", "0,0,0", "187,187,187"
	};
#else
    for (i = 0; i < 22; i++) {
	static const char *const defaults[] = {
	    "187,187,187", "255,255,255", "0,0,0", "85,85,85", "0,0,0",
	    "0,255,0", "0,0,0", "85,85,85", "187,0,0", "255,85,85",
	    "0,187,0", "85,255,85", "187,187,0", "255,255,85", "0,0,187",
	    "85,85,255", "187,0,187", "255,85,255", "0,187,187",
	    "85,255,255", "187,187,187", "255,255,255"
	};
#endif
	char buf[20], *buf2;
	int c0, c1, c2;
	sprintf(buf, "Colour%d", i);
	buf2 = gpps_raw_forced(sesskey, buf, defaults[i]);
	if (sscanf(buf2, "%d,%d,%d", &c0, &c1, &c2) == 3) {
	    conf_set_int_int(conf, CONF_colours, i*3+0, c0);
	    conf_set_int_int(conf, CONF_colours, i*3+1, c1);
	    conf_set_int_int(conf, CONF_colours, i*3+2, c2);
	}
	sfree(buf2);
    }
    gppi_forced(sesskey, "RawCNP", 0, conf, CONF_rawcnp);
    gppi_forced(sesskey, "PasteRTF", 0, conf, CONF_rtf_paste);
    gppi_forced(sesskey, "MouseIsXterm", 0, conf, CONF_mouse_is_xterm);
    gppi_forced(sesskey, "RectSelect", 0, conf, CONF_rect_select);
    gppi_forced(sesskey, "MouseOverride", 1, conf, CONF_mouse_override);
    for (i = 0; i < 256; i += 32) {
	static const char *const defaults[] = {
	    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0",
	    "0,1,2,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1",
	    "1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,2",
	    "1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1",
	    "1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1",
	    "1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1",
	    "2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2",
	    "2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2"
	};
	char buf[20], *buf2, *p;
	int j;
	sprintf(buf, "Wordness%d", i);
	buf2 = gpps_raw_forced(sesskey, buf, defaults[i / 32]);
	p = buf2;
	for (j = i; j < i + 32; j++) {
	    char *q = p;
	    while (*p && *p != ',')
		p++;
	    if (*p == ',')
		*p++ = '\0';
	    conf_set_int_int(conf, CONF_wordness, j, atoi(q));
	}
	sfree(buf2);
    }
    /*
     * The empty default for LineCodePage will be converted later
     * into a plausible default for the locale.
     */
    gpps_forced(sesskey, "LineCodePage", "", conf, CONF_line_codepage);
    gppi_forced(sesskey, "CJKAmbigWide", 0, conf, CONF_cjk_ambig_wide);
    gppi_forced(sesskey, "UTF8Override", 1, conf, CONF_utf8_override);
    gpps_forced(sesskey, "Printer", "", conf, CONF_printer);
#ifdef PRINTCLIPPORT
    if( !strcmp( conf_get_str(conf,CONF_printer),PRINT_TO_CLIPBOARD_STRING) ) { conf_set_int(conf,CONF_printclip,1) ; }
    else { conf_set_int(conf,CONF_printclip,0); }
#endif
    gppi_forced(sesskey, "CapsLockCyr", 0, conf, CONF_xlat_capslockcyr);
    gppi_forced(sesskey, "ScrollBar", 1, conf, CONF_scrollbar);
    gppi_forced(sesskey, "ScrollBarFullScreen", 0, conf, CONF_scrollbar_in_fullscreen);
    gppi_forced(sesskey, "ScrollOnKey", 0, conf, CONF_scroll_on_key);
    gppi_forced(sesskey, "ScrollOnDisp", 1, conf, CONF_scroll_on_disp);
    gppi_forced(sesskey, "EraseToScrollback", 1, conf, CONF_erase_to_scrollback);
    gppi_forced(sesskey, "LockSize", 0, conf, CONF_resize_action);
    gppi_forced(sesskey, "BCE", 1, conf, CONF_bce);
    gppi_forced(sesskey, "BlinkText", 0, conf, CONF_blinktext);
    gppi_forced(sesskey, "X11Forward", 0, conf, CONF_x11_forward);
    gpps_forced(sesskey, "X11Display", "", conf, CONF_x11_display);
    gppi_forced(sesskey, "X11AuthType", X11_MIT, conf, CONF_x11_auth);
    gppfile_forced(sesskey, "X11AuthFile", conf, CONF_xauthfile);
    gppi_forced(sesskey, "LocalPortAcceptAll", 0, conf, CONF_lport_acceptall);
    gppi_forced(sesskey, "RemotePortAcceptAll", 0, conf, CONF_rport_acceptall);
    gppmap_forced(sesskey, "PortForwardings", conf, CONF_portfwd);
    i = gppi_raw_forced(sesskey, "BugIgnore1", 0); conf_set_int(conf, CONF_sshbug_ignore1, 2-i);
    i = gppi_raw_forced(sesskey, "BugPlainPW1", 0); conf_set_int(conf, CONF_sshbug_plainpw1, 2-i);
    i = gppi_raw_forced(sesskey, "BugRSA1", 0); conf_set_int(conf, CONF_sshbug_rsa1, 2-i);
    i = gppi_raw_forced(sesskey, "BugIgnore2", 0); conf_set_int(conf, CONF_sshbug_ignore2, 2-i);
    {
	int i;
	i = gppi_raw_forced(sesskey, "BugHMAC2", 0); conf_set_int(conf, CONF_sshbug_hmac2, 2-i);
	if (2-i == AUTO) {
	    i = gppi_raw_forced(sesskey, "BuggyMAC", 0);
	    if (i == 1)
		conf_set_int(conf, CONF_sshbug_hmac2, FORCE_ON);
	}
    }
    i = gppi_raw_forced(sesskey, "BugDeriveKey2", 0); conf_set_int(conf, CONF_sshbug_derivekey2, 2-i);
    i = gppi_raw_forced(sesskey, "BugRSAPad2", 0); conf_set_int(conf, CONF_sshbug_rsapad2, 2-i);
    i = gppi_raw_forced(sesskey, "BugPKSessID2", 0); conf_set_int(conf, CONF_sshbug_pksessid2, 2-i);
    i = gppi_raw_forced(sesskey, "BugRekey2", 0); conf_set_int(conf, CONF_sshbug_rekey2, 2-i);
    i = gppi_raw_forced(sesskey, "BugMaxPkt2", 0); conf_set_int(conf, CONF_sshbug_maxpkt2, 2-i);
    i = gppi_raw_forced(sesskey, "BugOldGex2", 0); conf_set_int(conf, CONF_sshbug_oldgex2, 2-i);
    i = gppi_raw_forced(sesskey, "BugWinadj", 0); conf_set_int(conf, CONF_sshbug_winadj, 2-i);
    i = gppi_raw_forced(sesskey, "BugChanReq", 0); conf_set_int(conf, CONF_sshbug_chanreq, 2-i);
    conf_set_int(conf, CONF_ssh_simple, FALSE);
    gppi_forced(sesskey, "StampUtmp", 1, conf, CONF_stamp_utmp);
    gppi_forced(sesskey, "LoginShell", 1, conf, CONF_login_shell);
    gppi_forced(sesskey, "ScrollbarOnLeft", 0, conf, CONF_scrollbar_on_left);
    gppi_forced(sesskey, "ShadowBold", 0, conf, CONF_shadowbold);
    gppfont_forced(sesskey, "BoldFont", conf, CONF_boldfont);
    gppfont_forced(sesskey, "WideFont", conf, CONF_widefont);
    gppfont_forced(sesskey, "WideBoldFont", conf, CONF_wideboldfont);
    gppi_forced(sesskey, "ShadowBoldOffset", 1, conf, CONF_shadowboldoffset);
    gpps_forced(sesskey, "SerialLine", "", conf, CONF_serline);
    gppi_forced(sesskey, "SerialSpeed", 9600, conf, CONF_serspeed);
    gppi_forced(sesskey, "SerialDataBits", 8, conf, CONF_serdatabits);
    gppi_forced(sesskey, "SerialStopHalfbits", 2, conf, CONF_serstopbits);
    gppi_forced(sesskey, "SerialParity", SER_PAR_NONE, conf, CONF_serparity);
    gppi_forced(sesskey, "SerialFlowControl", SER_FLOW_XONXOFF, conf, CONF_serflow);
    gpps_forced(sesskey, "WindowClass", "", conf, CONF_winclass);
    gppi_forced(sesskey, "ConnectionSharing", 0, conf, CONF_ssh_connection_sharing);
    gppi_forced(sesskey, "ConnectionSharingUpstream", 1, conf, CONF_ssh_connection_sharing_upstream);
    gppi_forced(sesskey, "ConnectionSharingDownstream", 1, conf, CONF_ssh_connection_sharing_downstream);
    gppmap_forced(sesskey, "SSHManualHostKeys", conf, CONF_ssh_manual_hostkeys);
/* rutty: */
#ifdef RUTTYPORT
	gppfile_forced(sesskey, "ScriptFileName", conf, CONF_script_filename);
	gppi_forced(sesskey, "ScriptMode", 0, conf, CONF_script_mode);
	gppi_forced(sesskey, "ScriptLineDelay", 0, conf, CONF_script_line_delay);
	gppi_forced(sesskey, "ScriptCharDelay", 0, conf, CONF_script_char_delay);
	gpps_forced(sesskey, "ScriptCondLine", ":", conf, CONF_script_cond_line);
	gppi_forced(sesskey, "ScriptCondUse", 0, conf, CONF_script_cond_use);
	gppi_forced(sesskey, "ScriptCRLF", SCRIPT_NOLF, conf, CONF_script_crlf);
	gppi_forced(sesskey, "ScriptEnable", 0, conf, CONF_script_enable);
	gppi_forced(sesskey, "ScriptExcept", 0, conf, CONF_script_except);
	gppi_forced(sesskey, "ScriptTimeout", 30, conf, CONF_script_timeout);
	gpps_forced(sesskey, "ScriptWait", "", conf, CONF_script_waitfor);
	gpps_forced(sesskey, "ScriptHalt", "", conf, CONF_script_halton);
#endif  /* rutty */  
#ifdef RECONNECTPORT
    gppi_forced(sesskey, "WakeupReconnect", 0, conf, CONF_wakeup_reconnect /*&cfg->wakeup_reconnect*/);
    gppi_forced(sesskey, "FailureReconnect", 0, conf, CONF_failure_reconnect /*&cfg->failure_reconnect*/);
#endif
#if (defined IMAGEPORT) && (!defined FDJ)
    gppi_forced(sesskey, "BgOpacity", 50, conf, CONF_bg_opacity /*&cfg->bg_opacity*/);
    gppi_forced(sesskey, "BgSlideshow", 0, conf, CONF_bg_slideshow /*&cfg->bg_slideshow*/);
    gppi_forced(sesskey, "BgType", 0, conf, CONF_bg_type /*&cfg->bg_type*/);
    gppfile_forced(sesskey, "BgImageFile", conf, CONF_bg_image_filename /*&cfg->bg_image_filename*/);
    gppi_forced(sesskey, "BgImageStyle", 0, conf, CONF_bg_image_style /*&cfg->bg_image_style*/);
    gppi_forced(sesskey, "BgImageAbsoluteX", 0, conf, CONF_bg_image_abs_x /*&cfg->bg_image_abs_x*/);
    gppi_forced(sesskey, "BgImageAbsoluteY", 0, conf, CONF_bg_image_abs_y /*&cfg->bg_image_abs_y*/);
    gppi_forced(sesskey, "BgImagePlacement", 0, conf, CONF_bg_image_abs_fixed /*&cfg->bg_image_abs_fixed*/);
#endif
#ifdef URLPORT
    gppi_forced(sesskey, "CopyURLDetection", 0, conf, CONF_copy_clipbd_url_reg /*cfg->copy_clipbd_url_reg*/);
#endif
#ifdef HYPERLINKPORT
	/*
	 * HACK: PuttyTray / Nutty
	 * Hyperlink stuff: Load hyperlink settings
	 */
	gppi_forced(sesskey, "HyperlinkUnderline", 1, conf, CONF_url_underline);
	gppi_forced(sesskey, "HyperlinkUseCtrlClick", 0, conf, CONF_url_ctrl_click);
	gppi_forced(sesskey, "HyperlinkBrowserUseDefault", 1, conf, CONF_url_defbrowser);
	gppfile_forced(sesskey, "HyperlinkBrowser", conf, CONF_url_browser);
	gppi_forced(sesskey, "HyperlinkRegularExpressionUseDefault", 1, conf, CONF_url_defregex);

#ifndef NO_HYPERLINK
	gpps_forced(sesskey, "HyperlinkRegularExpression", urlhack_default_regex, conf, CONF_url_regex);
#endif
#endif
#ifdef IVPORT
    /* Background */
    for (i = 0; i < 4; i++) {
	static const char *const defaults[] = {
	    "100,100", "100,100", "100,100", "100,100"
	};
	static const int CONF_alphas_pc[4][2] = {
	    CONF_alphas_pc_cursor_active,
	    CONF_alphas_pc_cursor_inactive,
	    CONF_alphas_pc_defauly_fg_active,
	    CONF_alphas_pc_defauly_fg_inactive,
	    CONF_alphas_pc_degault_bg_active,
	    CONF_alphas_pc_degault_bg_inactive,
	    CONF_alphas_pc_bg_active,
	    CONF_alphas_pc_bg_inactive
	};
	char buf[16];
	char *buf2;
	int c0 = 100;
	int c1 = 100;
	sprintf(buf, "Alpha%d", i);
	buf2 = gpps_raw_forced(sesskey, buf, defaults[i]);
	if (sscanf(buf2, "%d,%d", &c0, &c1)) {
	    if (c0 > 100) {
		c0 = 100;
	    }
	    if (c1 > 100) {
		c1 = 100;
	    }
	    conf_set_int(conf, CONF_alphas_pc[i][0], c0);
	    conf_set_int(conf, CONF_alphas_pc[i][1], c1);
	}
	sfree(buf2);
    }
    gppi_forced(sesskey, "BackgroundWallpaper", 0, conf, CONF_bg_wallpaper);
    gppi_forced(sesskey, "BackgroundEffect", 0, conf, CONF_bg_effect);
    gppfile_forced(sesskey, "WallpaperFile", conf, CONF_wp_file);
    gppi_forced(sesskey, "WallpaperPosition", 0, conf, CONF_wp_position);
    gppi_forced(sesskey, "WallpaperAlign", 0, conf, CONF_wp_align);
    gppi_forced(sesskey, "WallpaperVerticalAlign", 0, conf, CONF_wp_valign);
    gppi_forced(sesskey, "WallpaperMoving", 0, conf, CONF_wp_moving);
#endif
#ifdef CYGTERMPORT
    gppi_forced(sesskey, "CygtermAltMetabit", 0, conf, CONF_alt_metabit);
    gppi_forced(sesskey, "CygtermAutoPath", 1, conf, CONF_cygautopath );
    gppi_forced(sesskey, "Cygterm64", 0, conf, CONF_cygterm64);
    gpps_forced(sesskey, "CygtermCommand", "", conf, CONF_cygcmd );
#endif
#ifdef ZMODEMPORT
    gppfile_forced(sesskey, "rzCommand", conf, CONF_rzcommand );
    if( strlen(conf_get_filename( conf, CONF_rzcommand)->path) == 0 ) { conf_set_filename(conf,CONF_rzcommand,conf_get_filename(confDef,CONF_rzcommand)) ; }
    gpps_forced(sesskey, "rzOptions", conf_get_str(confDef, CONF_rzoptions), conf, CONF_rzoptions );
    gppfile_forced(sesskey, "szCommand", conf, CONF_szcommand );
    if( strlen(conf_get_filename( conf, CONF_szcommand)->path) == 0 ) { conf_set_filename(conf,CONF_szcommand,conf_get_filename(confDef,CONF_szcommand)) ; }
    gpps_forced(sesskey, "szOptions",  conf_get_str(confDef, CONF_szoptions), conf, CONF_szoptions );
    //gpps_forced(sesskey, "zDownloadDir", "C:\\", conf, CONF_zdownloaddir );
    gpps_forced(sesskey, "zDownloadDir", conf_get_str(confDef, CONF_zdownloaddir) , conf, CONF_zdownloaddir );
#endif
#ifdef PERSOPORT
    gppi_forced(sesskey, "TransparencyValue", 0, conf, CONF_transparencynumber /*&cfg->transparencynumber*/ ) ;
    if( conf_get_int( conf, CONF_transparencynumber) /*cfg->transparencynumber*/ < -1 ) conf_set_int( conf,CONF_transparencynumber,-1) /*cfg->transparencynumber = -1*/;
    if( conf_get_int( conf, CONF_transparencynumber) /*cfg->transparencynumber*/ > 255 ) conf_set_int( conf,CONF_transparencynumber,255) /*cfg->transparencynumber = 255*/;
    gppi_forced(sesskey, "SendToTray", 0, conf, CONF_sendtotray /*&cfg->sendtotray*/);
    gppi_forced(sesskey, "Maximize", 0, conf, CONF_maximize /*&cfg->maximize*/);
    gppi_forced(sesskey, "Fullscreen", 0, conf, CONF_fullscreen /*&cfg->fullscreen*/);
    gppi_forced(sesskey, "SaveOnExit", 0, conf, CONF_saveonexit /*&cfg->saveonexit*/);
    gppi_forced(sesskey, "Icone", 1, conf, CONF_icone /*&cfg->icone*/);
    //gpps_forced(sesskey, "IconeFile", "", conf, CONF_iconefile /*cfg->iconefile, sizeof(cfg->iconefile)*/);
    gppfile_forced(sesskey, "IconeFile", conf, CONF_iconefile /*cfg->iconefile, sizeof(cfg->iconefile)*/);
    gpps_forced(sesskey, "SFTPConnect", "", conf, CONF_sftpconnect );
    gppfile_forced(sesskey, "Scriptfile", conf, CONF_scriptfile /*&cfg->scriptfile*/);
    Filename * fn = filename_from_str( "" ) ;
    conf_set_filename(conf,CONF_scriptfile,fn);
    filename_free(fn);
    gpps_forced(sesskey, "ScriptfileContent", "", conf, CONF_scriptfilecontent );
    gpps_forced(sesskey, "AntiIdle", "", conf, CONF_antiidle /*cfg->antiidle, sizeof(cfg->antiidle)*/);
    gpps_forced(sesskey, "LogTimestamp", "", conf, CONF_logtimestamp /*cfg->logtimestamp, sizeof(cfg->logtimestamp)*/);
    gpps_forced(sesskey, "Autocommand", "", conf, CONF_autocommand /*cfg->autocommand, sizeof(cfg->autocommand)*/);
    gpps_forced(sesskey, "AutocommandOut", "", conf, CONF_autocommandout /*cfg->autocommandout, sizeof(cfg->autocommandout)*/);
    gpps_forced(sesskey, "Folder", "", conf, CONF_folder /*cfg->folder, sizeof(cfg->folder)*/);
    gppi_forced(sesskey, "LogTimeRotation", 0, conf, CONF_logtimerotation /*&cfg->logtimerotation*/);
    gppi_forced(sesskey, "TermXPos", -1, conf, CONF_xpos /*&cfg->xpos*/);
    gppi_forced(sesskey, "TermYPos", -1, conf, CONF_ypos /*&cfg->ypos*/);
    gppi_forced(sesskey, "WindowState", 0, conf, CONF_windowstate /*&cfg->windowstate*/);
    gppi_forced(sesskey, "SaveWindowPos", 0, conf, CONF_save_windowpos /*&cfg->save_windowpos*/); /* BKG */
    gppi_forced(sesskey, "ForegroundOnBell", 0, conf, CONF_foreground_on_bell /*&cfg->foreground_on_bell*/);
#ifndef NO_PASSWORD
    if( strlen(conf_get_str(conf, CONF_host))>0 ) {
	if( (strlen(conf_get_str(conf, CONF_host))+strlen(conf_get_str(conf, CONF_termtype))) < 1000 ) { 
		sprintf( PassKey, "%s%sKiTTY", conf_get_str(conf, CONF_host), conf_get_str(conf, CONF_termtype) ) ;
	} else {
		strcpy( PassKey, "" ) ;
	}
	gpps_forced(sesskey, "Password", "", conf, CONF_password );
    } else { conf_set_str( conf, CONF_password, "" ) ; } 

    if( strlen(conf_get_str(conf, CONF_password))>0 ) {
	char pst[4096] ;
	if( strlen(conf_get_str(conf, CONF_password))<=4095 ) { strcpy( pst, conf_get_str(conf, CONF_password) ) ; }
	else { memcpy( pst, conf_get_str( conf, CONF_password ), 4095 ) ; pst[4095]='\0'; }
	decryptstring( pst, PassKey ) ;

	MASKPASS(pst);
	conf_set_str( conf, CONF_password, pst ) ;
	memset(pst,0,strlen(pst));
    }
#else
	conf_set_str( conf, CONF_password, "" ) ;
#endif
    gppi_forced(sesskey, "CtrlTabSwitch", 0, conf, CONF_ctrl_tab_switch);
    gpps_forced(sesskey, "Comment", "", conf, CONF_comment );
    gppi_forced(sesskey, "ACSinUTF", 0, conf, CONF_acs_in_utf);
    gppi_forced(sesskey, "SCPAutoPwd", 0, conf, CONF_scp_auto_pwd);
#endif
#ifdef PORTKNOCKINGPORT
	gpps_forced(sesskey, "PortKnocking", "", conf, CONF_portknockingoptions );
#endif
#ifdef DISABLEALTGRPORT
	gppi_forced(sesskey, "DisableAltGr", 0, conf, CONF_disablealtgr);
#endif
// END COPY/PASTE
	conf_set_str( conf, CONF_folder, "Default") ;
	fclose(sesskey) ;
		
	conf_free( confDef ) ;
}






/*****
FONCTIONS SUPPORT
*****/

static int CryptFileFlag = 0 ;
int SwitchCryptFlag( void ) { CryptFileFlag = abs( CryptFileFlag -1 ) ; return CryptFileFlag ; }

void mungestr(const char *in, char *out);
void unmungestr(char *in, char *out, int outlen) ;

void write_setting_i_forced(void *handle, const char *key, int value) {
	char buf[1024] ;
	sprintf( buf, "%s\\%i\\", key, value ) ;
	if( CryptFileFlag ) { cryptstring(buf,MASTER_PASSWORD); }
	//fprintf( (FILE*)handle, "%s\\%i\\\n", key, value ) ;
	fprintf( (FILE*)handle, "%s\n", buf ) ;
}

void write_setting_s_forced(void *handle, const char *key, const char *value) {
	char *p ;
	p=(char*)malloc( 3*strlen(value)+256 );
	mungestr(value, p);
	char * buf ;
	buf = (char*) malloc( 2*(strlen(key)+strlen(p))+10 ) ;
	sprintf( buf, "%s\\%s\\", key, p ) ;
	if( CryptFileFlag ) { cryptstring(buf,MASTER_PASSWORD); }
	fprintf( (FILE*)handle, "%s\n", buf ) ;
	free(buf);
	free(p);
}

void write_setting_filename_forced(void *handle, const char *key, Filename *value) {
	char *p ;
	p=(char*)malloc( 3*strlen(value->path)+256 );
	mungestr(value->path, p);
	char * buf ;
	buf = (char*) malloc( 2*(strlen(key)+strlen(p))+10 ) ;
	sprintf( buf, "%s\\%s\\", key, p ) ;
	if( CryptFileFlag ) { cryptstring(buf,MASTER_PASSWORD); }
	fprintf( (FILE*)handle, "%s\n", buf ) ;
	free(buf) ;
	free(p);
}

void write_setting_fontspec_forced(void *handle, const char *name, FontSpec *font) {
	char *settingname;
    write_setting_s_forced(handle, name, font->name);
    settingname = dupcat(name, "IsBold", NULL);
    write_setting_i_forced(handle, settingname, font->isbold);
    sfree(settingname);
    settingname = dupcat(name, "CharSet", NULL);
    write_setting_i_forced(handle, settingname, font->charset);
    sfree(settingname);
    settingname = dupcat(name, "Height", NULL);
    write_setting_i_forced(handle, settingname, font->height);
    sfree(settingname);
}

static void wmap_forced(void *handle, char const *outkey, Conf *conf, int primary,int include_values) {
    char *buf, *p, *key, *realkey;
    const char *val, *q;
    int len;

    len = 1;			       /* allow for NUL */

    for (val = conf_get_str_strs(conf, primary, NULL, &key);
	 val != NULL;
	 val = conf_get_str_strs(conf, primary, key, &key))
	len += 2 + 2 * (strlen(key) + strlen(val));   /* allow for escaping */

    buf = snewn(len, char);
    p = buf;

    for (val = conf_get_str_strs(conf, primary, NULL, &key);
	 val != NULL;
	 val = conf_get_str_strs(conf, primary, key, &key)) {

        if (primary == CONF_portfwd && !strcmp(val, "D")) {
            /*
             * Backwards-compatibility hack, as above: translate from
             * the sensible internal representation of dynamic
             * forwardings (key "L<port>", value "D") to the
             * conceptually incoherent legacy storage format (key
             * "D<port>", value empty).
             */
            char *L;

            realkey = key;             /* restore it at end of loop */
            val = "";
            key = dupstr(key);
            L = strchr(key, 'L');
            if (L) *L = 'D';
        } else {
            realkey = NULL;
        }

	if (p != buf)
	    *p++ = ',';
	for (q = key; *q; q++) {
	    if (*q == '=' || *q == ',' || *q == '\\')
		*p++ = '\\';
	    *p++ = *q;
	}
        if (include_values) {
            *p++ = '=';
            for (q = val; *q; q++) {
                if (*q == '=' || *q == ',' || *q == '\\')
                    *p++ = '\\';
                *p++ = *q;
            }
        }

        if (realkey) {
            free(key);
            key = realkey;
        }
    }
    *p = '\0';
    write_setting_s_forced(handle, outkey, buf);
    sfree(buf);
}

static void wprefs_forced(void *sesskey, const char *name, const struct keyvalwhere *mapping, int nvals, Conf *conf, int primary) {
    char *buf, *p;
    int i, maxlen;

    for (maxlen = i = 0; i < nvals; i++) {
	const char *s = val2key(mapping, nvals,
                                conf_get_int_int(conf, primary, i));
	if (s) {
            maxlen += (maxlen > 0 ? 1 : 0) + strlen(s);
        }
    }

    buf = snewn(maxlen + 1, char);
    p = buf;

    for (i = 0; i < nvals; i++) {
	const char *s = val2key(mapping, nvals,
                                conf_get_int_int(conf, primary, i));
	if (s) {
            p += sprintf(p, "%s%s", (p > buf ? "," : ""), s);
	}
    }

    assert(p - buf == maxlen);
    *p = '\0';

    write_setting_s_forced(sesskey, name, buf);

    sfree(buf);
}

int read_setting_i_forced(void *handle, const char *key, int defvalue) {
	int n = defvalue ;
	char buffer[2048], name[256] ;
	rewind(handle);
	sprintf( name, "%s\\", key ) ;
	while( fgets(buffer,2047,handle)!=NULL ) {
		while( (buffer[strlen(buffer)-1]=='\n')||(buffer[strlen(buffer)-1]=='\r') ) buffer[strlen(buffer)-1]='\0' ;
		if( buffer[strlen(buffer)-1] != '\\' ) { decryptstring( buffer, MASTER_PASSWORD) ; }
		if( strstr( buffer, name ) == buffer ) {
			while( (buffer[strlen(buffer)-1]=='\\')||(buffer[strlen(buffer)-1]=='\n')||(buffer[strlen(buffer)-1]=='\r') ) buffer[strlen(buffer)-1]='\0' ;
			n = atoi( buffer+strlen(name) ) ;
			break ;
		}
	}
	return n ;
}

char *read_setting_s_forced(void *handle, const char *key) {
	char * loadResult = NULL ;
	char buffer[2048], name[256] ;
	rewind(handle);
	sprintf( name, "%s\\", key ) ;
	
	while( fgets(buffer,2047,handle)!=NULL ) {
		while( (buffer[strlen(buffer)-1]=='\n')||(buffer[strlen(buffer)-1]=='\r') ) buffer[strlen(buffer)-1]='\0' ;
		if( buffer[strlen(buffer)-1] != '\\' ) { decryptstring( buffer, MASTER_PASSWORD) ; }
		if( strstr( buffer, name ) == buffer ) {
			while( (buffer[strlen(buffer)-1]=='\\')||(buffer[strlen(buffer)-1]=='\n')||(buffer[strlen(buffer)-1]=='\r') ) buffer[strlen(buffer)-1]='\0' ;
			loadResult = (char*) malloc( strlen( buffer+strlen(name) ) + 1 ) ;
			unmungestr( buffer+strlen(name), loadResult, strlen( buffer+strlen(name) ) + 1 ) ;
			break ;
		}
	}
	return loadResult ;
}

Filename *read_setting_filename_forced(void *handle, const char *key) {
	Filename * Result = NULL ;
	char buffer[2048], name[256] ;
	rewind(handle);
	sprintf( name, "%s\\", key ) ;
	while( fgets(buffer,2047,handle)!=NULL ) {
		while( (buffer[strlen(buffer)-1]=='\n')||(buffer[strlen(buffer)-1]=='\r') ) buffer[strlen(buffer)-1]='\0' ;
		if( buffer[strlen(buffer)-1] != '\\' ) { decryptstring( buffer, MASTER_PASSWORD) ; }
		if( strstr( buffer, name ) == buffer ) {
			while( (buffer[strlen(buffer)-1]=='\\')||(buffer[strlen(buffer)-1]=='\n')||(buffer[strlen(buffer)-1]=='\r') ) buffer[strlen(buffer)-1]='\0' ;
			unmungestr( buffer+strlen(name), buffer, 2047 ) ;
			Result = filename_from_str( buffer ) ;
			break ;
		}
	}
	return Result ;
}

#include <limits.h>
FontSpec *read_setting_fontspec_forced(void *handle, const char *name)
{
    char *settingname;
    char *fontname;
    FontSpec *ret;
    int isbold, height, charset;

    fontname = read_setting_s_forced(handle, name);
    if (!fontname)
	return NULL;

    settingname = dupcat(name, "IsBold", NULL);
    isbold = read_setting_i_forced(handle, settingname, -1);
    sfree(settingname);
    if (isbold == -1) {
        sfree(fontname);
        return NULL;
    }

    settingname = dupcat(name, "CharSet", NULL);
    charset = read_setting_i_forced(handle, settingname, -1);
    sfree(settingname);
    if (charset == -1) {
        sfree(fontname);
        return NULL;
    }

    settingname = dupcat(name, "Height", NULL);
    height = read_setting_i_forced(handle, settingname, INT_MIN);
    sfree(settingname);
    if (height == INT_MIN) {
        sfree(fontname);
        return NULL;
    }

    ret = fontspec_new(fontname, isbold, height, charset);
    sfree(fontname);
    return ret;
}




static void gppi_forced(void *handle, const char *name, int def, Conf *conf, int primary) {
    conf_set_int(conf, primary, gppi_raw_forced(handle, name, def));
}

static int gppi_raw_forced(void *handle, const char *name, int def) {
    def = platform_default_i(name, def);
    return read_setting_i_forced(handle, name, def);
}

static void gppfile_forced(void *handle, const char *name, Conf *conf, int primary) {
    Filename *result = read_setting_filename_forced(handle, name);
    if (!result)
	result = platform_default_filename(name);
    conf_set_filename(conf, primary, result);
    filename_free(result);
}

static void gpps_forced(void *handle, const char *name, const char *def, Conf *conf, int primary) {
    char *val = gpps_raw_forced(handle, name, def);
    conf_set_str(conf, primary, val);
    sfree(val);
}

static char *gpps_raw_forced(void *handle, const char *name, const char *def) {
    char *ret = read_setting_s_forced(handle, name);
    if (!ret)
	ret = platform_default_s(name);
    if (!ret)
	ret = def ? dupstr(def) : NULL;   /* permit NULL as final fallback */
    return ret;
}

static void gppfont_forced(void *handle, const char *name, Conf *conf, int primary) {
    FontSpec *result = read_setting_fontspec_forced(handle, name);
    if (!result)
        result = platform_default_fontspec(name);
    conf_set_fontspec(conf, primary, result);
    fontspec_free(result);
}

static int gppmap_forced(void *handle, const char *name, Conf *conf, int primary) {
    char *buf, *p, *q, *key, *val;

    /*
     * Start by clearing any existing subkeys of this key from conf.
     */
    while ((key = conf_get_str_nthstrkey(conf, primary, 0)) != NULL)
        conf_del_str_str(conf, primary, key);

    /*
     * Now read a serialised list from the settings and unmarshal it
     * into its components.
     */
    buf = gpps_raw_forced(handle, name, NULL);
    if (!buf)
	return FALSE;

    p = buf;
    while (*p) {
	q = buf;
	val = NULL;
	while (*p && *p != ',') {
	    int c = *p++;
	    if (c == '=')
		c = '\0';
	    if (c == '\\')
		c = *p++;
	    *q++ = c;
	    if (!c)
		val = q;
	}
	if (*p == ',')
	    p++;
	if (!val)
	    val = q;
	*q = '\0';

        if (primary == CONF_portfwd && strchr(buf, 'D') != NULL) {
            /*
             * Backwards-compatibility hack: dynamic forwardings are
             * indexed in the data store as a third type letter in the
             * key, 'D' alongside 'L' and 'R' - but really, they
             * should be filed under 'L' with a special _value_,
             * because local and dynamic forwardings both involve
             * _listening_ on a local port, and are hence mutually
             * exclusive on the same port number. So here we translate
             * the legacy storage format into the sensible internal
             * form, by finding the D and turning it into a L.
             */
            char *newkey = dupstr(buf);
            *strchr(newkey, 'D') = 'L';
            conf_set_str_str(conf, primary, newkey, "D");
            sfree(newkey);
        } else {
            conf_set_str_str(conf, primary, buf, val);
        }
    }
    sfree(buf);

    return TRUE;
}

static void gprefs_forced(void *sesskey, const char *name, const char *def, const struct keyvalwhere *mapping, int nvals, Conf *conf, int primary) {
    /*
     * Fetch the string which we'll parse as a comma-separated list.
     */
    char *value = gpps_raw_forced(sesskey, name, def);
    gprefs_from_str(value, mapping, nvals, conf, primary);
    sfree(value);
}

