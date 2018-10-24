// Probleme du bug de consommation memoire avec le shrink => 
//	- voir 0/1 dans la fonction RedrawBackground
//	- voir dans le fichier WINDOW.C le  if((UINT_PTR)wParam == TIMER_REDRAW)
// Le probleme est dans load_file_jpeg => il manquait un GlobalFree


// Essai de compilation séparée
#ifdef FDJ
#undef IMAGEPORT
#endif

#ifdef IMAGEPORT
#ifdef NO
#include <windows.h>
#include "putty.h"
#include "terminal.h"

extern Conf *conf ;// extern Config cfg;
//extern int offset_width, offset_height ;
//extern int font_width, font_height ;

#ifndef NCFGCOLOURS
#define NCFGCOLOURS 24
#endif
#ifndef NEXTCOLOURS
#define NEXTCOLOURS 240
#endif
#ifndef NALLCOLOURS
#define NALLCOLOURS (NCFGCOLOURS + NEXTCOLOURS)
#endif

//extern COLORREF colours[NALLCOLOURS] ;
extern HWND MainHwnd ;

int stricmp(const char *s1, const char *s2) ;
int GetSessionField( const char * session_in, const char * folder_in, const char * field, char * result ) ;
int get_param( const char * val ) ;

int return_offset_height(void) ;
int return_offset_width(void) ;
int return_font_height(void) ;
int return_font_width(void) ;
COLORREF return_colours258(void) ;

#endif

static BOOL (WINAPI * pAlphaBlend)( HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION ) = 0 ;

//static HWND hwnd;
#include "kitty_image.h"

HDC textdc = NULL ;
HBITMAP textbm = NULL ;
COLORREF colorinpixel;
HDC colorinpixeldc = NULL ;
HBITMAP colorinpixelbm = NULL;
HDC backgrounddc = NULL ;
HBITMAP backgroundbm = NULL ;
HDC backgroundblenddc = NULL ;
HBITMAP backgroundblendbm = NULL;
BOOL bBgRelToTerm;
int resizing;
RECT size_before;


#ifdef DLL
#define TARGET extern __declspec(dllexport)
#else
#define TARGET
#endif

static int ShrinkBitmapEnable = 1 ;
void SetShrinkBitmapEnable( int v ) {
	if( v ) ShrinkBitmapEnable = 1 ;
	else ShrinkBitmapEnable = 0 ;
}
	

//
// Fonctions de shrink de bitmap
//
#define Alloc(p,t) (t *)malloc((p)*sizeof(t))
#define For(i,n) for ((i)=0;(i)<(n);(i)++)
#define iFor(n) For (i,n)
#define jFor(n) For (j,n)

typedef struct {
	WORD x, y ;	// dimensions
	WORD l ;	// bytes per scan-line (32-bit allignment)
	BYTE *b ;	// bits of bitmap,3 bytes/pixel, BGR
} tWorkBMP ;		// 24-bit working bitmap

void CreateWorkingBitmap( WORD dx, WORD dy, tWorkBMP *w ) {
	w->x=dx ;
	w->y=dy ;
	w->l=(dx+1)*3&0xfffc ;
	w->b=Alloc( w->l*dy, BYTE ) ;
}

HBITMAP CreateEmptyBitmap( WORD dx, WORD dy ) {
	HDC h = GetDC( NULL ) ;
	HBITMAP b = CreateCompatibleBitmap( h, dx, dy ) ;
	ReleaseDC( NULL, h ) ;
	return(b) ;
}

void SetBMIHeader( BITMAPINFO *b, short dx, short dy ) {
	b->bmiHeader.biSize = sizeof(BITMAPINFOHEADER) ;
	b->bmiHeader.biWidth = dx ;
	b->bmiHeader.biHeight = -dy ;
	b->bmiHeader.biPlanes = 1 ;
	b->bmiHeader.biBitCount = 24 ;
	b->bmiHeader.biCompression = BI_RGB ;
	b->bmiHeader.biSizeImage = 0 ;
	b->bmiHeader.biXPelsPerMeter = 1 ;
	b->bmiHeader.biYPelsPerMeter = 1 ;
	b->bmiHeader.biClrUsed = 0 ;
	b->bmiHeader.biClrImportant = 0 ;
}

POINT GetBitmapSize( HBITMAP h ) {
	POINT p ;
	BITMAP o ;
	GetObject( h, sizeof(o), &o ) ;
	p.x = o.bmWidth ;
	p.y = o.bmHeight ;
	return(p) ;
}

void OpenBitmapForWork( HBITMAP b, tWorkBMP *w ) {
	BITMAPINFO s ;
	HDC h = GetDC( NULL ) ;
	POINT v = GetBitmapSize( b ) ;
	CreateWorkingBitmap( v.x, v.y, w ) ;
	SetBMIHeader( &s,w->x, w->y ) ;
	GetDIBits( h, b, 0, w->y, w->b, &s, DIB_RGB_COLORS ) ;
	ReleaseDC( NULL, h ) ;
}

void SaveWorkingBitmap( tWorkBMP *w, HBITMAP b ) {
	BITMAPINFO s ;
	HDC h = GetDC( NULL ) ;
	SetBMIHeader( &s, w->x, w->y ) ;
	SetDIBits( h, b, 0, w->y, w->b, &s, DIB_RGB_COLORS ) ;
	ReleaseDC( NULL, h ) ;
}

void ShrinkWorkingBitmap( tWorkBMP *a, tWorkBMP *b, WORD bx, WORD by ) {
	BYTE *uy = a->b, *ux, i ;
	WORD x, y, nx, ny = 0 ;
	DWORD df = 3*bx, nf = df*by, j ;
	float k, qx[2], qy[2], q[4], *f = Alloc( nf, float ) ;

	CreateWorkingBitmap( bx, by, b) ;

	jFor (nf) f[j]=0;
	j=0;

	For( y, a->y ) {
		ux=uy;
		uy+=a->l;
		nx=0;
		ny+=by;
		if (ny>a->y) {
			qy[0]=1-(qy[1]=(ny-a->y)/(float)by);
			For (x,a->x) {
				nx+=bx;
				if (nx>a->x) {
					qx[0]=1-(qx[1]=(nx-a->x)/(float)bx);
					iFor (4) q[i]=qx[i&1]*qy[i>>1];
					iFor (3) {
						f[j]+=(*ux)*q[0];
						f[j+3]+=(*ux)*q[1];
						f[j+df]+=(*ux)*q[2];
						f[(j++)+df+3]+=(*(ux++))*q[3];
					}
				} else iFor (3) {
					f[j+i]+=(*ux)*qy[0];
					f[j+df+i]+=(*(ux++))*qy[1];
				}
				if (nx>=a->x) nx-=a->x;
				if (!nx) j+=3;
			}
		} else {
			For (x,a->x) {
				nx+=bx;
				if (nx>a->x) {
					qx[0]=1-(qx[1]=(nx-a->x)/(float)bx);
					iFor (3) {
						f[j]+=(*ux)*qx[0];
						f[(j++)+3]+=(*(ux++))*qx[1];
					}
				}
				else iFor (3) f[j+i]+=*(ux++);
				if (nx>=a->x) nx-=a->x;
				if (!nx) j+=3;
			}
			if (ny<a->y) j-=df;
		}
		if (ny>=a->y) ny-=a->y;
	}

	nf=0;
	k=bx*by/(float)(a->x*a->y);
	uy=b->b;

	For (y,by) {
		jFor (df) uy[j]=(unsigned char)(f[nf++]*k+.5);
		uy+=b->l;
	}

	free (f);
}

TARGET HBITMAP ShrinkBitmap( HBITMAP a, WORD bx, WORD by )
// creates and returns new bitmap with dimensions of
// [bx,by] by shrinking bitmap a both [bx,by] must be less or equal
// than the dims of a, unless the result is nonsense
{
	tWorkBMP in, out ;
	HBITMAP b=CreateEmptyBitmap( bx, by ) ;
	OpenBitmapForWork( a, &in ) ;
	ShrinkWorkingBitmap( &in, &out, bx, by ) ;
	free( in.b ) ;
	SaveWorkingBitmap( &out, b ) ;
	free( out.b ) ;
	return( b ) ;
}






HBITMAP ResizeBmp( HBITMAP hBmpSrc, WORD bx, WORD by ) {
	SIZE newSize ;
	newSize.cx = bx;
	newSize.cy = by;
	// taille actuelle
	BITMAP bmpInfo;
	GetObject(hBmpSrc, sizeof(BITMAP), &bmpInfo);
	SIZE oldSize;
	oldSize.cx = bmpInfo.bmWidth;
	oldSize.cy = bmpInfo.bmHeight;

	// selection source ds un DC
	HDC hdc = GetDC(NULL);
	HDC hDCSrc = CreateCompatibleDC(hdc);
	HBITMAP hOldBmpSrc = (HBITMAP)SelectObject(hDCSrc, hBmpSrc);

	// création bitmap dest et sélection ds un DC
	HDC hDCDst = CreateCompatibleDC(hdc);
	HBITMAP hBmpDst = CreateCompatibleBitmap(hdc, newSize.cx, newSize.cy);
	HBITMAP hOldBmpDst = (HBITMAP)SelectObject(hDCDst, hBmpDst);

	// resize
	StretchBlt(hDCDst, 0, 0, newSize.cx, newSize.cy, hDCSrc, 0, 0, oldSize.cx, oldSize.cy, SRCCOPY);
	
	// libération ressources
	SelectObject(hDCSrc, hOldBmpSrc);
	SelectObject(hDCDst, hOldBmpDst);
	DeleteDC(hDCSrc);
	DeleteDC(hDCDst);
	ReleaseDC(NULL, hdc);
	return hBmpDst;
}

static void fill_dc(HDC dc, int width, int height, COLORREF color)
{
    HBRUSH clrBrush = CreateSolidBrush(color);
    HPEN clrPen = CreatePen(PS_SOLID, 0, color);

    HBRUSH oldBrush;
    HPEN oldPen;

    oldBrush = SelectObject(dc, clrBrush);
    oldPen = SelectObject(dc, clrPen);

    Rectangle(dc, 0, 0, width, height);

    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
}

static BOOL load_wallpaper_bmp(HBITMAP* rawImage, int* style, int* x, int* y)
{
    LONG lRes;
    HKEY kDesktop;
    DWORD pathLen = MAX_PATH;
    DWORD numBufLen = 10;
    char wpPath[MAX_PATH];
    char wpStyleBuf[10];
    char wpTileBuf[10];

    int wpStyle = -1;
    int wpTile = -1;

    // NOTE: In the non-wallpaper case (i.e., load_file_bmp), we load parameters
    // like x, y and style from cfg, but in the wallpaper case we ignore our
    // stored configuration and get that information from the system.

    // ENHANCE: It's possible to set WallpaperOriginX and WallpaperOriginY to
    // specify an exact position for the start of the wallpaper, but this
    // function doesn't support that yet.  I don't think it's possible to set
    // it through the normal UI anyway, you have to hack the registry to do it.
    // For now, we'll never return an (x,y) positioning request to the caller.
    *x = *y = 0;

    lRes = RegOpenKeyEx(
        HKEY_CURRENT_USER, "Control Panel\\Desktop", 0, KEY_READ, &kDesktop
    );
    if(lRes != ERROR_SUCCESS)
    {
        RegCloseKey(kDesktop);
        return FALSE; // TODO: Should the error be reported to the user here?
    }

    lRes = RegQueryValueEx(kDesktop, "Wallpaper", NULL, NULL, (LPBYTE)wpPath, &pathLen);
    if(lRes != ERROR_SUCCESS)
    {
        RegCloseKey(kDesktop);
        return FALSE; // TODO: Should the error be reported to the user here?
    }

    lRes = RegQueryValueEx( kDesktop, "WallpaperStyle", NULL, NULL, (LPBYTE)wpStyleBuf, &numBufLen );
    if(lRes == ERROR_SUCCESS)
        wpStyle = atoi(wpStyleBuf);

    lRes = RegQueryValueEx( kDesktop, "TileWallpaper", NULL, NULL, (LPBYTE)wpTileBuf, &numBufLen );
    if(lRes == ERROR_SUCCESS)
        wpTile = atoi(wpTileBuf);

    if(wpStyle < 0 && wpStyle > 3)
        wpStyle = 0;  // Default to tile.
    else if(wpTile > 0)
        wpStyle = 0;  // Force tile.
    else if(wpStyle == 0 && wpTile == 0)
        wpStyle = 1;  // For Explorer, wpStyle == wpTile == 0 means center, and
                      // it doesn't ever set wpStyle to 1.  We call wpStyle == 1
                      // center and don't use wpTile, to simplify things after
                      // this point.

    RegCloseKey(kDesktop);
 
    if( *rawImage!=NULL ) { DeleteObject( *rawImage ) ; *rawImage=NULL ; }
    *rawImage = LoadImage(
        NULL, wpPath, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE
    );
    if(*rawImage == 0)
        return FALSE; // TODO: Should the error be reported to the user here?

    *style = wpStyle;

    return TRUE;
}

static BOOL load_file_bmp(HBITMAP* rawImage, int* style, int* x, int* y)
{
    *x = conf_get_int( conf,CONF_bg_image_abs_x); // cfg.bg_image_abs_x;
    *y = conf_get_int( conf,CONF_bg_image_abs_y ); // cfg.bg_image_abs_y;
    *style = conf_get_int( conf,CONF_bg_image_style); // cfg.bg_image_style;

    if( *rawImage!=NULL ) { DeleteObject( *rawImage ) ; *rawImage=NULL ; }
    *rawImage = LoadImage(
        NULL, conf_get_filename( conf, CONF_bg_image_filename )/*cfg.bg_image_filename.*/->path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE
    );
    if(*rawImage == 0)
        return FALSE; // TODO: Should the error be reported to the user here?

    return TRUE;
}

#include <setjmp.h>
#include "jpeg/jpeglib.h"
jmp_buf JPEG_bailout;
int usePalette = 0;
char *loadError = NULL;	
COLORREF skycolour = RGB(0, 0, 255);

HBITMAP CreateHBitmap(int w, int h, LPVOID *lpBits)
{
	HBITMAP bitmap;
	BITMAPINFOHEADER BIH ;
	int iSize = sizeof(BITMAPINFOHEADER) ;
	memset(&BIH, 0, iSize);

	// Fill in the header info.
	BIH.biSize = iSize;
	BIH.biWidth = w;
	BIH.biHeight = h;
	BIH.biPlanes = 1;
	BIH.biBitCount = 24;
	BIH.biCompression = BI_RGB;
	HDC hDC = CreateCompatibleDC(NULL);
	bitmap = CreateDIBSection(hDC,
							(BITMAPINFO*)&BIH,
							DIB_RGB_COLORS,
							lpBits,
							NULL,
							0);
	DeleteDC(hDC);
	return bitmap;
}

//  LOADJPEGIMAGE  --  Load JPEG image into memory
HBITMAP loadJPEGimage(FILE *input_file, HGLOBAL *LimageBitmap, int *LsizeX, int *LsizeY)
{
	int i;
	LPBITMAPINFOHEADER bh;
	DWORD bmpsize;
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	JSAMPARRAY colormap;
	LPBYTE pix;
	int linewid;
	int pixbytes;
	static LPBYTE sl = NULL;
	static HGLOBAL imageBitmap = NULL;		// In-memory bitmap

	sl = NULL;
	imageBitmap = NULL;
	if (setjmp(JPEG_bailout) != 0) {
		/*	Since we arrive here via longjmp() from
			parts unknown, we may have allocated
			the line buffer or bitmap prior to bailing
			out.  If they've been allocated, release them.  */

		if (sl != NULL) {
			GlobalFree(sl);
			sl = NULL;
		}
		if (imageBitmap != NULL) {
			GlobalFree(imageBitmap);
			imageBitmap = NULL;
		}
		return NULL;
	}
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, input_file);
	jpeg_read_header(&cinfo, TRUE);
	cinfo.desired_number_of_colors = 254;
	cinfo.quantize_colors = usePalette;
	jpeg_start_decompress(&cinfo);

	pixbytes = (usePalette ? 1 : 3);
	sl = GlobalAlloc(GMEM_FIXED, cinfo.output_width * pixbytes);
	if (sl == NULL) {
		loadError = "Cannot allocate JPEG decoder row buffer";
		return NULL;
	}

	linewid = ((((cinfo.output_width * pixbytes) + (sizeof(LONG) - 1)) / sizeof(LONG)) * sizeof(LONG));
	bmpsize = sizeof(BITMAPINFOHEADER) +
		(usePalette ? (256 * sizeof(RGBQUAD)) : 0) +
		(linewid * cinfo.output_height);

	imageBitmap = GlobalAlloc(GMEM_FIXED, bmpsize);
	if (imageBitmap == NULL) {
		loadError = "Cannot allocate bitmap for decoded JPEG image";
		GlobalFree(sl);
		return NULL;
	}

	//	Plug in header fields with information from cinfo

	bh = (LPBITMAPINFOHEADER) imageBitmap;
	pix = ((LPBYTE) imageBitmap) + sizeof(BITMAPINFOHEADER) +
			(usePalette ? (256 * sizeof(RGBQUAD)) : 0);
	bh->biSize = sizeof(BITMAPINFOHEADER);
	bh->biWidth = cinfo.output_width;
	bh->biHeight = cinfo.output_height;
	bh->biPlanes = 1;
	bh->biBitCount = usePalette ? 8 : 24;
	bh->biCompression = BI_RGB;
	bh->biSizeImage = 0;
	bh->biXPelsPerMeter = bh->biYPelsPerMeter = 2835;
	bh->biClrUsed = 0;
	bh->biClrImportant = 0;

	/*	Construct the palette from the colour map optimised
		for the JPEG file.  */

	if (usePalette) {
		colormap = cinfo.colormap;
		for (i = 0; i < cinfo.actual_number_of_colors; i++) {
			if (cinfo.num_components == 1) {
				((LPRGBQUAD) &((LPBITMAPINFO) bh)->bmiColors[i])->rgbRed =
				((LPRGBQUAD) &((LPBITMAPINFO) bh)->bmiColors[i])->rgbGreen =
				((LPRGBQUAD) &((LPBITMAPINFO) bh)->bmiColors[i])->rgbBlue = GETJSAMPLE(colormap[0][i]);
				((LPRGBQUAD) &((LPBITMAPINFO) bh)->bmiColors[i])->rgbReserved = 0;
			} else {
				((LPRGBQUAD) &((LPBITMAPINFO) bh)->bmiColors[i])->rgbRed = GETJSAMPLE(colormap[0][i]);
				((LPRGBQUAD) &((LPBITMAPINFO) bh)->bmiColors[i])->rgbGreen = GETJSAMPLE(colormap[1][i]);
				((LPRGBQUAD) &((LPBITMAPINFO) bh)->bmiColors[i])->rgbBlue = GETJSAMPLE(colormap[2][i]);
				((LPRGBQUAD) &((LPBITMAPINFO) bh)->bmiColors[i])->rgbReserved = 0;
			}
		}

		/*	Plug black and our text colour in the last two slots
			of the palette so we canbe sure they're available.  */
	
		((LPRGBQUAD) &((LPBITMAPINFO) bh)->bmiColors[254])->rgbRed =
		((LPRGBQUAD) &((LPBITMAPINFO) bh)->bmiColors[254])->rgbGreen =
		((LPRGBQUAD) &((LPBITMAPINFO) bh)->bmiColors[254])->rgbBlue =
		((LPRGBQUAD) &((LPBITMAPINFO) bh)->bmiColors[254])->rgbReserved = 0;

		((LPRGBQUAD) &((LPBITMAPINFO) bh)->bmiColors[255])->rgbRed = GetRValue(skycolour);
		((LPRGBQUAD) &((LPBITMAPINFO) bh)->bmiColors[255])->rgbGreen = GetGValue(skycolour);
		((LPRGBQUAD) &((LPBITMAPINFO) bh)->bmiColors[255])->rgbBlue = GetBValue(skycolour);
		((LPRGBQUAD) &((LPBITMAPINFO) bh)->bmiColors[255])->rgbReserved = 0;
	}

	//	Return scan lines and transfer to the pixel array

	BYTE *pDst = NULL;
	HBITMAP hBitmap = CreateHBitmap(bh->biWidth, bh->biHeight, (void**)&pDst);
	int nStorageWidth = ((bh->biWidth * 24 + 31) & ~31) >> 3; //dword alignment
	JSAMPARRAY ppDst = &pDst;
/*
	px = pix + (linewid * (bh->biHeight - 1));
	for (i = 0; i < (int) cinfo.output_height; i++) {
		unsigned char *slp[1] = { sl };
		jpeg_read_scanlines(&cinfo, slp, 1);
		if (usePalette) {
			memcpy(px, sl, cinfo.output_width);
		} else {
			int j, k;

			if (cinfo.num_components == 3) {
				for (j = k = 0; j < (int) cinfo.output_width; j++, k += 3) {
					px[k] = sl[k + 2];
					px[k + 1] = sl[k + 1];
					px[k + 2] = sl[k];
				}
			} else {
				for (j = k = 0; j < (int) cinfo.output_width; j++, k += 3) {
					px[k] = sl[j];
					px[k + 1] = sl[j];
					px[k + 2] = sl[j];
				}
			}
		}
		px -= linewid;
	}
*/
pDst = pDst + (cinfo.output_height-1)*nStorageWidth ;
while (cinfo.output_scanline < cinfo.output_height) {
      jpeg_read_scanlines (&cinfo, ppDst, 1);

	  if(cinfo.out_color_components==3) {
		  //swap R & B
		  BYTE* p = pDst;
		  int i ;
		  for( i=0;i<bh->biWidth;i++) {
			  BYTE r = p[0];
			  p[0] = p[2];
			  p[2] = r;
			  p += 3;
		  }
	  }

	  //pDst += nStorageWidth;
	  pDst -= nStorageWidth;
	}

	GlobalFree(sl);
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	 
	*LsizeX = (int) bh->biWidth;
	*LsizeY = (int) bh->biHeight;
	*LimageBitmap = imageBitmap;
	return hBitmap;
}


static BOOL load_file_jpeg(HBITMAP* rawImage, int* style, int* x, int* y) {
    *x = conf_get_int( conf, CONF_bg_image_abs_x ); // cfg.bg_image_abs_x;
    *y = conf_get_int( conf, CONF_bg_image_abs_y ); // cfg.bg_image_abs_y;
    *style = conf_get_int( conf, CONF_bg_image_style ); // cfg.bg_image_style;
    int res=TRUE, LsizeX, LsizeY ;
    FILE *fp ;
    HGLOBAL LimageBitmap = NULL ;

	
    if(  ( fp=fopen( conf_get_filename( conf,CONF_bg_image_filename)/*cfg.bg_image_filename.*/->path, "rb" ) ) == NULL ) return FALSE ;
    
    if( *rawImage!=NULL ) { DeleteObject( *rawImage ) ; *rawImage=NULL ; }
*rawImage = loadJPEGimage(fp, &LimageBitmap,&LsizeX, &LsizeY) ;
if( rawImage == NULL ) res =FALSE ;    
     fclose( fp ) ;

    GlobalFree(LimageBitmap);
    
    return res;
}

HBITMAP HWND_to_HBITMAP(HWND hWnd)
{
  RECT    r;
  HDC     hdcMem, hdcScr;
  HBITMAP hbmMem, hbmOld;
 
  GetWindowRect(hWnd, &r);
  hdcScr = GetWindowDC(hWnd);
  hdcMem = CreateCompatibleDC(hdcScr);
  hbmMem = CreateCompatibleBitmap(hdcScr, r.right -= r.left, r.bottom -= r.top) ;
  hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);
  BitBlt(hdcMem, 0, 0, r.right, r.bottom, hdcScr, 0, 0, SRCCOPY);
  SelectObject(hdcMem, hbmOld);
  ReleaseDC(hWnd, hdcScr);
  DeleteDC(hdcMem);
  return hbmMem;
}
 
BOOL HBITMAP_to_JPG(HBITMAP hbm, LPCTSTR jpgfile, int quality)
{
  BITMAP      bm;
  BITMAPINFO  bi;
  BYTE       *pPixels;
  JSAMPROW    jrows[1], jrow;
  HDC         hdcScr, hdcMem1, hdcMem2;
  HBITMAP     hbmMem, hbmOld1, hbmOld2;
  FILE       *fp = fopen(jpgfile, "wb");
  struct jpeg_compress_struct jpeg;
  struct jpeg_error_mgr       jerr;
 
  if(!hbm)
    return 0;
  if(!GetObject(hbm, sizeof(bm), &bm))
    return 0;
  if(!fp)
    return 0;
 
  ZeroMemory(&bi, sizeof(bi));
  bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth       = bm.bmWidth;
  bi.bmiHeader.biHeight      = bm.bmHeight;
  bi.bmiHeader.biPlanes      = 1;
  bi.bmiHeader.biBitCount    = 24;
  bi.bmiHeader.biCompression = BI_RGB;
 
  hdcScr  = GetDC(0);
  hdcMem1 = CreateCompatibleDC(hdcScr);
  hbmOld1 =  (HBITMAP)SelectObject(hdcMem1,hbm);
  hdcMem2 = CreateCompatibleDC(hdcScr);
  hbmMem  = CreateDIBSection(hdcScr, &bi, DIB_RGB_COLORS, (VOID **)&pPixels, 0, 0);
  hbmOld2 = (HBITMAP)SelectObject(hdcMem2, hbmMem);
 
  BitBlt(hdcMem2, 0, 0, bm.bmWidth, bm.bmHeight, hdcMem1, 0, 0, SRCCOPY);
 
  SelectObject(hdcMem1, hbmOld1);
  SelectObject(hdcMem2, hbmOld2);
  ReleaseDC(0, hdcScr);
  DeleteDC(hdcMem1);
  DeleteDC(hdcMem2);
 
  jpeg.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&jpeg);
  jpeg_stdio_dest(&jpeg, fp);
  jpeg.image_width      = bm.bmWidth;
  jpeg.image_height     = bm.bmHeight;
  jpeg.input_components = 3;
  jpeg.in_color_space   = JCS_RGB;
  jpeg.dct_method       = JDCT_FLOAT;
  jpeg_set_defaults(&jpeg);
  jpeg_set_quality(&jpeg, (quality < 0 || quality > 100) ? 100 : quality, TRUE);
  jpeg_start_compress(&jpeg, TRUE);
 
  char *comment=NULL ;
  if( comment ) {
	jpeg_write_marker(&jpeg, JPEG_COM, (const JOCTET*)comment, strlen(comment));
  }

  while(jpeg.next_scanline < jpeg.image_height)
  {
    unsigned int i, j, tmp;
 
    jrow = &pPixels[(jpeg.image_height - jpeg.next_scanline - 1) * ((((bm.bmWidth * 24) + 31) / 32) * 4)];
 
    for(i = 0; i < jpeg.image_width; i++)
    {
      j           = i * 3;
      tmp         = jrow[j];
      jrow[j]     = jrow[j + 2];
      jrow[j + 2] = tmp;
    }
    jrows[0] = jrow;
    jpeg_write_scanlines(&jpeg, jrows, 1);
  }
 
  jpeg_finish_compress(&jpeg);
  jpeg_destroy_compress(&jpeg);
  DeleteObject(hbmMem);
  fclose(fp);
  return 1;
}

void MakeScreenShot() {
HBITMAP hbm = HWND_to_HBITMAP(GetDesktopWindow());
   if(hbm) {
    HBITMAP_to_JPG(hbm, "screenshot.jpg", 85) ;
    DeleteObject(hbm);
  }
}

static HBITMAP CreateDIBSectionWithFileMapping(HDC dc, int width, int height, HANDLE fmap)
{
    BITMAPINFOHEADER BMI;
    
    BMI.biSize = sizeof(BITMAPINFOHEADER);
    BMI.biWidth = width;
    BMI.biHeight = height;
    BMI.biPlanes = 1;
    BMI.biBitCount = 32;
    BMI.biCompression = BI_RGB;
    BMI.biSizeImage = 0;
    BMI.biXPelsPerMeter = 0;
    BMI.biYPelsPerMeter = 0;
    BMI.biClrUsed = 0;
    BMI.biClrImportant = 0;
    
    return(CreateDIBSection(dc, (BITMAPINFO *)&BMI, DIB_RGB_COLORS, 0, fmap, 0));
}


/***********SCREEN CAPTURE*******************/
int screenCapturePart(int x, int y, int w, int h, LPCSTR fname,int quality) {
    int return_code = 0 ;
    HDC hdcSource = GetDC(NULL);
    HDC hdcMemory = CreateCompatibleDC(hdcSource);

    //int capX = GetDeviceCaps(hdcSource, HORZRES);
    //int capY = GetDeviceCaps(hdcSource, VERTRES);

    HBITMAP hBitmap = CreateCompatibleBitmap(hdcSource, w, h);
    HBITMAP hBitmapOld = (HBITMAP)SelectObject(hdcMemory, hBitmap);

    BitBlt(hdcMemory, 0, 0, w, h, hdcSource, x, y, SRCCOPY);
    hBitmap = (HBITMAP)SelectObject(hdcMemory, hBitmapOld);

    DeleteDC(hdcSource);
    DeleteDC(hdcMemory);

    //HPALETTE hpal = NULL;
    if( HBITMAP_to_JPG( hBitmap,fname, quality) ) { return_code = 1 ; }
    DeleteObject(hBitmap);
	
    return return_code ;
}
int screenCaptureClientRect( HWND hwnd, LPCSTR fname, int quality ) {
	RECT rc;
	POINT p;
	GetClientRect(hwnd, &rc);
	p.x=rc.left; p.y=rc.top,
	ClientToScreen(hwnd,&p);
	rc.left=p.x;rc.top=p.y;
	return screenCapturePart(rc.left,rc.top,rc.right,rc.bottom,fname,quality) ;
}
int screenCaptureWinRect( HWND hwnd, LPCSTR fname, int quality ) {
	RECT r;
	GetWindowRect(hwnd, &r);
	return screenCapturePart(r.left,r.top,r.right-r.left,r.bottom-r.top,fname,quality) ;
}
int screenCaptureAll( LPCSTR fname, int quality ) {
	return screenCapturePart(0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics (SM_CYSCREEN),fname,quality) ;
}

/******************************/

void init_dc_blend(void) 
{
    //HMODULE * msimg32_dll = LoadLibrary("msimg32.dll");
    HMODULE msimg32_dll = LoadLibrary("msimg32.dll");
    
    if(msimg32_dll) 
        pAlphaBlend = GetProcAddress(msimg32_dll, "AlphaBlend");
    
    if(pAlphaBlend) 
    {
    	HDC hdc = GetDC(hwnd);
    	
    	// Create one pixel size bitmap for use in color_blend.
        if( colorinpixeldc !=NULL ) DeleteDC(colorinpixeldc ); colorinpixeldc = CreateCompatibleDC(hdc);
        if( colorinpixelbm!=NULL ) DeleteObject(colorinpixelbm); colorinpixelbm = CreateCompatibleBitmap(hdc, 1, 1);
        SelectObject(colorinpixeldc, colorinpixelbm);
        colorinpixel = 0;
        SetPixelV(colorinpixeldc, 0, 0, colorinpixel);
        
        ReleaseDC(hwnd, hdc);
    }
}

void color_blend(
    HDC destDc, int x, int y, int width, int height, 
    COLORREF alphacolor, int opacity)
{
    if(pAlphaBlend) {
    	// Fast alpha blending for Win98&2000 and newer...
        BLENDFUNCTION blender;

        // Create one pixel size bitmap for use in color_blend.
        if(colorinpixel != alphacolor) 
        {
            colorinpixel = alphacolor;
            SetPixelV(colorinpixeldc, 0, 0, alphacolor);
        }
        
        blender.BlendOp = AC_SRC_OVER;
        blender.BlendFlags = 0;
        blender.SourceConstantAlpha = (0xff * opacity) / 100;
        blender.AlphaFormat = 0;
        
        (*pAlphaBlend)(destDc, x, y, width, height, colorinpixeldc, 0, 0, 1, 1, blender);
    } 
    else
    {
        // Slow alpha blending for Win95&NT...
        // Note: Only tested with WinXP, should work on 95/NT.. probably.
        int i, alphacolorR, alphacolorG, alphacolorB, bk_opacity;
        HBITMAP tmpbm;
        HDC tmpdc;
        static HANDLE fmap;
        static int fmap_size;
        static unsigned char * pRGB;
        
        if(fmap_size < width * height * 4)
        {
            if(fmap) {
            	UnmapViewOfFile(pRGB);
                CloseHandle(fmap);
            }
            fmap_size = width * height * 4;
            fmap = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, fmap_size, NULL);
            pRGB = MapViewOfFile(fmap, FILE_MAP_ALL_ACCESS, 0, 0, fmap_size);
        }
        
        // Create DIBSection so we get pixels easily.
        tmpdc = CreateCompatibleDC(destDc);
        tmpbm = CreateDIBSectionWithFileMapping(destDc, width, height, fmap);
        SelectObject(tmpdc, tmpbm);
        
        // Copy bitmap to temporary bitmap for easy pixel access.
        BitBlt(tmpdc, 0, 0, width, height, destDc, x, y, SRCCOPY);

        // Moved stuff out from the loop
        alphacolorR = GetRValue(alphacolor) * opacity;
        alphacolorG = GetGValue(alphacolor) * opacity;
        alphacolorB = GetBValue(alphacolor) * opacity;
	bk_opacity = 100 - opacity;

        for(i=0; i<width*height*4; i+=4)
        {
	    pRGB[i + 0] = (pRGB[i + 0] * bk_opacity + alphacolorB) / 100;
            pRGB[i + 1] = (pRGB[i + 1] * bk_opacity + alphacolorG) / 100;
            pRGB[i + 2] = (pRGB[i + 2] * bk_opacity + alphacolorR) / 100;
        }
        
        // Copy temporary bitmap back to original
        BitBlt(destDc, x, y, width, height, tmpdc, 0, 0, SRCCOPY);
        
        DeleteObject(tmpbm);
        DeleteDC(tmpdc);
    }
}

#include <math.h>
static void color_opacity_gradient( HDC destDc, int x, int y, int width, int height, COLORREF alphacolor, int style ) {
	int i, alphacolorR, alphacolorG, alphacolorB, bk_opacity, opacity, h, w ;
	double l ;
        HBITMAP tmpbm;
        HDC tmpdc;
        static HANDLE fmap;
        static int fmap_size;
        static unsigned char * pRGB;
	int OpacityMin = 0, OpacityMax = 100 ;
	
	char buf[256] = "" ;
	if( GetSessionField( conf_get_str(conf,CONF_sessionname)/*cfg.sessionname*/, conf_get_str(conf,CONF_folder)/*cfg.folder*/, "BgOpacityRange", buf ) ) { 
		sscanf( buf, "%d-%d", &OpacityMin, &OpacityMax ) ; 
		if( OpacityMin == OpacityMax ) { OpacityMin = 0 ; OpacityMax = 100 ; }
		}
        
        if(fmap_size < width * height * 4) {
		if(fmap) { UnmapViewOfFile(pRGB) ; CloseHandle(fmap) ; }
		fmap_size = width * height * 4;
		fmap = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, fmap_size, NULL);
		pRGB = MapViewOfFile(fmap, FILE_MAP_ALL_ACCESS, 0, 0, fmap_size);
		}
        
        // Create DIBSection so we get pixels easily.
        tmpdc = CreateCompatibleDC(destDc);
        tmpbm = CreateDIBSectionWithFileMapping(destDc, width, height, fmap);
        SelectObject(tmpdc, tmpbm);
        
        // Copy bitmap to temporary bitmap for easy pixel access.
        BitBlt(tmpdc, 0, 0, width, height, destDc, x, y, SRCCOPY);

        opacity = 0 ; bk_opacity = 100 ; alphacolorR = 0 ; alphacolorG = 0 ; alphacolorB = 0 ;
	w = 0 ; h = 0 ;

        for(i=0; i<width*height*4; i+=4) {
		
		switch( style ) {
			case 2: // De bas en haut
			if( (i%(4*width)) == 0 ) {
				h++ ;
				opacity = OpacityMin + 1.0*( OpacityMax-OpacityMin ) * (1.0*h)/(1.0*height) ;
				opacity = 100 - opacity ;
				}
				break ;
			case 3: // De gauche a droite
				w++ ; if( w >= width ) { w = 0 ; }
				opacity = OpacityMin + 1.0*( OpacityMax-OpacityMin ) * (1.0*w)/(1.0*width) ;
				break ;
			case 4: // De droite a gauche
				w++ ; if( w >= width ) { w = 0 ; }
				opacity = OpacityMin + 1.0*( OpacityMax-OpacityMin ) * (1.0*w)/(1.0*width) ;
				opacity = 100 - opacity ;
				break ;
			case 5: // Du centre vers l'exterieur
				if( (i%(4*width)) == 0 ) { h++ ; }
				w++ ; if( w >= width ) { w = 0 ; }
				l = sqrt( pow(1.0*width/2.0-w,2.0)+pow(1.0*height/2.0-h,2.0) ) / 
					sqrt( pow(1.0*width/2.0,2.0)+pow(1.0*height/2.0,2.0) );
				opacity = OpacityMin + 1.0*( OpacityMax-OpacityMin ) * l ;
				break ;
			case 6: // De l'exterieur vers le centre
				if( (i%(4*width)) == 0 ) { h++ ; }
				w++ ; if( w >= width ) { w = 0 ; }
				l = sqrt( pow(1.0*width/2.0-w,2.0)+pow(1.0*height/2.0-h,2.0) ) / 
					sqrt( pow(1.0*width/2.0,2.0)+pow(1.0*height/2.0,2.0) );
				opacity = OpacityMin + 1.0*( OpacityMax-OpacityMin ) * l ;
				opacity = 100 - opacity ;					
				break ;
			case 7:
				if( (i%(4*width)) == 0 ) { h++ ; }
				w++ ; if( w >= width ) { w = 0 ; }
				l = sqrt( pow(1.0*w,2.0)+pow(1.0*h,2.0) )/sqrt( pow(1.0*width,2.0)+pow(1.0*height,2.0) ) ;
				opacity = OpacityMin + 1.0*( OpacityMax-OpacityMin ) * l ;
				break ;
			case 8:
				if( (i%(4*width)) == 0 ) { h++ ; }
				w++ ; if( w >= width ) { w = 0 ; }
				l = sqrt( pow(1.0*w,2.0)+pow(1.0*h,2.0) )/sqrt( pow(1.0*width,2.0)+pow(1.0*height,2.0) ) ;
				opacity = OpacityMin + 1.0*( OpacityMax-OpacityMin ) * l ;
				opacity = 100 - opacity ;
				break ;
			case 9:
				if( (i%(4*width)) == 0 ) { h++ ; }
				w++ ; if( w >= width ) { w = 0 ; }
				l = sqrt( pow(1.0*(width-w),2.0)+pow(1.0*h,2.0) )/sqrt( pow(1.0*width,2.0)+pow(1.0*height,2.0) ) ;
				opacity = OpacityMin + 1.0*( OpacityMax-OpacityMin ) * l ;
				break ;
			case 10:
				if( (i%(4*width)) == 0 ) { h++ ; }
				w++ ; if( w >= width ) { w = 0 ; }
				l = sqrt( pow(1.0*(width-w),2.0)+pow(1.0*h,2.0) )/sqrt( pow(1.0*width,2.0)+pow(1.0*height,2.0) ) ;
				opacity = OpacityMin + 1.0*( OpacityMax-OpacityMin ) * l ;
				opacity = 100 - opacity ;
				break ;
			default: // De haut en bas
			if( (i%(4*width)) == 0 ) {
				h++ ;
				opacity = OpacityMin + 1.0*( OpacityMax-OpacityMin ) * (1.0*h)/(1.0*height) ;
				}
				break ;
			}
		if( opacity < 0 ) opacity = 0 ;
		if( opacity > 100 ) opacity = 100 ;

		alphacolorR = GetRValue(alphacolor) * opacity;
		alphacolorG = GetGValue(alphacolor) * opacity;
		alphacolorB = GetBValue(alphacolor) * opacity;
		bk_opacity = 100 - opacity ;

		pRGB[i + 0] = (pRGB[i + 0] * bk_opacity + alphacolorB) / 100;
		pRGB[i + 1] = (pRGB[i + 1] * bk_opacity + alphacolorG) / 100;
		pRGB[i + 2] = (pRGB[i + 2] * bk_opacity + alphacolorR) / 100;
		}
        
        // Copy temporary bitmap back to original
        BitBlt(destDc, x, y, width, height, tmpdc, 0, 0, SRCCOPY);
        
        DeleteObject(tmpbm);
        DeleteDC(tmpdc);
	}

void CreateBlankBitmap( HBITMAP * rawImage, const int width, const int height ) {
	HDC dc= CreateCompatibleDC(NULL);
	BITMAPINFO bi;
	ZeroMemory( &bi.bmiHeader, sizeof(BITMAPINFOHEADER) );
	bi.bmiHeader.biWidth=width;     // Set size you need
	bi.bmiHeader.biHeight=height;    // Set size you need
	bi.bmiHeader.biPlanes=1;
	bi.bmiHeader.biBitCount=24; // Can be 8, 16, 32 bpp or
	bi.bmiHeader.biSizeImage=0;
	bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biClrUsed= 0;
	bi.bmiHeader.biClrImportant= 0;
	VOID *pvBits;
	
	if( *rawImage!=NULL ) { DeleteObject( *rawImage ) ; *rawImage=NULL ; }
	*rawImage = CreateDIBSection( dc,&bi,DIB_RGB_COLORS,&pvBits,NULL,0 );
	ReleaseDC(NULL, dc) ;

	HDC hDC = GetDC(hwnd);
	HDC hDCDst = CreateCompatibleDC(hDC); // memory device context for dest	bitmap
	ReleaseDC(NULL, hDC);

	// hdcMem contains your rendered particle
	
	HBRUSH hBrush = CreateSolidBrush(RGB(255,0,0));
	// Paints the rectangular band with the brush
	RECT Rect = {0, 0, width-1, height-1};
	FillRect(hDCDst, &Rect, hBrush);
	// Deletes the brush
	DeleteObject(hBrush);

	HGDIOBJ holdDIBDst = SelectObject(hDCDst, *rawImage);	
	
	// transfer the image to your DIB bitmap
	BitBlt(hDCDst, 0, 0 ,width, height, hDC, 0, 0, SRCCOPY);

	// clean up
	SelectObject(hDCDst, holdDIBDst);
	DeleteDC(hDCDst);
	}
	
BOOL load_bg_bmp()
{
    BOOL bRes;
    HBITMAP rawImage = NULL;
    BITMAP rawImageInfo;
    HDC hdcPrimary;
    HDC bmpdc;
    int deskWidth, deskHeight, clientWidth, clientHeight;
    int x, y;
    int style;

    // TODO: If cfg.bg_image_style == 1, we should use the system wallpaper
    // background color instead of this.  Probably just pass this as a parameter
    // to load_wallpaper_bmp so it can override this default while it's
    // accessing the registry anyway.
	
    //COLORREF backgroundcolor = colours[258]; // Default Background
    COLORREF backgroundcolor = return_colours258() ;
    COLORREF alphacolor = backgroundcolor;

    // Start off assuming this is true.
    bBgRelToTerm = conf_get_int( conf,CONF_bg_image_abs_fixed); // cfg.bg_image_abs_fixed;
	
    RECT clientRect;
    GetWindowRect( MainHwnd, &clientRect ) ;
    clientWidth = clientRect.right-clientRect.left+1 ;
    clientHeight = clientRect.bottom-clientRect.top+1 ;

    switch( conf_get_int( conf,CONF_bg_type)/*cfg.bg_type*/ )
    {
        // Solid
    case 0:
        // No bitmap file to load.  We'll handle this case below.
        break;

        // Wallpaper
    case 1:
        backgroundcolor = GetSysColor(COLOR_BACKGROUND);
        bBgRelToTerm = FALSE; // Wallpaper is never positioned relative to term.
        if(!load_wallpaper_bmp(&rawImage, &style, &x, &y))
            rawImage = NULL; // Make sure rawImage is still NULL.
        break;

        // Image
    case 2:
    	{
	backgroundcolor = GetSysColor(COLOR_BACKGROUND) ;
	if( conf_get_filename(conf,CONF_bg_image_filename)/*cfg.bg_image_filename.*/->path[0] == '#' ) {
		int r=0,g=0,b=0;
		sscanf( conf_get_filename(conf,CONF_bg_image_filename)/*cfg.bg_image_filename.*/->path, "#%02X%02X%02X", &r, &g, &b ) ;
		backgroundcolor = RGB( r, g, b ) ;
		BYTE *pDst = NULL;
		rawImage = CreateHBitmap(10, 10, (void**)&pDst);
		style = 4 ;
		}
    	else if( !stricmp( conf_get_filename(conf,CONF_bg_image_filename)/*cfg.bg_image_filename.*/->path+strlen(conf_get_filename(conf,CONF_bg_image_filename)/*cfg.bg_image_filename.*/->path)-4, ".jpg" ) ) {
    		if(!load_file_jpeg(&rawImage, &style, &x, &y))
        	    rawImage = NULL; // Make sure rawImage is still NULL.
    		}
    	else if( !stricmp( conf_get_filename(conf,CONF_bg_image_filename)/*cfg.bg_image_filename.*/->path+strlen(conf_get_filename(conf,CONF_bg_image_filename)/*cfg.bg_image_filename.*/->path)-5, ".jpeg" ) ) {
    		if(!load_file_jpeg(&rawImage, &style, &x, &y))
        	    rawImage = NULL; // Make sure rawImage is still NULL.
    		}
    	else 
        if(!load_file_bmp(&rawImage, &style, &x, &y))
            rawImage = NULL; // Make sure rawImage is still NULL.
        break;
	}
    }

    hdcPrimary = GetDC(hwnd);
    deskWidth = GetDeviceCaps(hdcPrimary, HORZRES);
    deskHeight = GetDeviceCaps(hdcPrimary, VERTRES);


	// Securite pour ne pas depacer les limites de l'ecran principal
//debug_log("%d %d %d %d %d %d %d\n",cfg.bg_image_abs_fixed,clientRect.left,clientRect.top,clientRect.right,clientRect.bottom,deskWidth,deskHeight );
	if( (bBgRelToTerm == 0) 
		&&((clientRect.right>deskWidth)||(clientRect.bottom>deskHeight)) ) {
		//DeleteObject(rawImage); rawImage = NULL ;
		bBgRelToTerm = 1 ;
		}

    if(rawImage == NULL)
    {
        // Create a solid bitmap.  We'll get here in two cases: either the user
        // selected the Solid background option, or the attempt to do something
        // fancier (use the system wallpaper or an image file) failed.
        /*
        if( backgrounddc!=NULL ) DeleteDC(backgrounddc) ; backgrounddc = CreateCompatibleDC(hdcPrimary);
        if( backgroundbm!=NULL ) DeleteObject(backgroundbm); backgroundbm
            = CreateCompatibleBitmap(hdcPrimary, deskWidth, deskHeight);
        SelectObject(backgrounddc, backgroundbm);
        */
        // Do not create anything, use default 'no background'-code instead.
    }
    else
    {
        // We've managed to load a good image.  Now, we need to manipulate it to
        // the right size, location, etc.

        // Find the width and height of the image we just loaded.
        GetObject(rawImage, sizeof(rawImageInfo), &rawImageInfo);

        // Create a temporary DC to wrap the raw image.
        bmpdc = CreateCompatibleDC(hdcPrimary);
        SelectObject(bmpdc, rawImage);

        // Create a memory DC that has a new bitmap of the appropriate final
        // image size.
        if( textdc!=NULL ) DeleteDC(textdc) ; textdc = CreateCompatibleDC(hdcPrimary);
        if( textbm!= NULL ) DeleteObject(textbm) ; textbm = CreateCompatibleBitmap(hdcPrimary, deskWidth, deskHeight);
        SelectObject(textdc, textbm);

        if( backgrounddc!=NULL ) DeleteDC(backgrounddc); backgrounddc = CreateCompatibleDC(hdcPrimary);
        if( backgroundbm!=NULL ) DeleteObject(backgroundbm); backgroundbm
            = CreateCompatibleBitmap(hdcPrimary, deskWidth, deskHeight);
        SelectObject(backgrounddc, backgroundbm);

	switch(style)
        {
        case 0: { // Tile
             for(y = 0; y < deskHeight; y += rawImageInfo.bmHeight)
            {
                for(x = 0; x < deskWidth; x += rawImageInfo.bmWidth)
                {
                    bRes = BitBlt(
                        backgrounddc,
                        x, y, rawImageInfo.bmWidth, rawImageInfo.bmHeight,
                        bmpdc, 0, 0, SRCCOPY
                    );
                }
            }
           }
           break;

        case 1:    // Center

            // Calculate x & y, ignoring values they may already have, then drop
            // down to the (X,Y) placement case below.

            x = (deskWidth - rawImageInfo.bmWidth) / 2;
            y = (deskHeight - rawImageInfo.bmHeight) / 2;

        case 3: {  // Absolute Place at given (X,Y)
            // Start out with a background color fill.
            fill_dc(backgrounddc, deskWidth, deskHeight, backgroundcolor);

            bRes = BitBlt(
                backgrounddc,
                x, y, rawImageInfo.bmWidth, rawImageInfo.bmHeight,
                bmpdc, 0, 0, SRCCOPY
            );
            break;
            }

        case 2: // Stretch
            bRes = StretchBlt(
                backgrounddc, 0, 0, deskWidth, deskHeight,
                bmpdc, 0, 0, rawImageInfo.bmWidth, rawImageInfo.bmHeight,
                SRCCOPY
            );

            break;

	case 4: // blank background
		fill_dc(backgrounddc, deskWidth, deskHeight, backgroundcolor) ;
		break ;
	
	case 5: // Stretch a la taille de la fenetre
		{
		if( (ShrinkBitmapEnable)&&(clientWidth<rawImageInfo.bmWidth)&&(clientHeight<rawImageInfo.bmHeight) ) {
			HBITMAP newhbmpBMP ;
			if( (newhbmpBMP = ShrinkBitmap( rawImage,clientWidth,clientHeight)) != NULL ) {
			//if( (newhbmpBMP = ResizeBmp( rawImage,clientWidth,clientHeight)) != NULL ) {
				DeleteDC(bmpdc) ;
				bmpdc = CreateCompatibleDC(0) ;
				DeleteDC(backgrounddc); backgrounddc = GetDC(hwnd);
				SelectObject(bmpdc, newhbmpBMP ) ;
				BitBlt(backgrounddc, 0, 0,clientWidth,clientHeight, bmpdc, 0, 0, SRCCOPY ) ;
				DeleteObject(newhbmpBMP);
				}
			else
			bRes = StretchBlt( backgrounddc,0,0,clientWidth,clientHeight,bmpdc,0,0,rawImageInfo.bmWidth,rawImageInfo.bmHeight,SRCCOPY);
			}
		else
			bRes = StretchBlt( backgrounddc,0,0,clientWidth,clientHeight,bmpdc,0,0,rawImageInfo.bmWidth,rawImageInfo.bmHeight,SRCCOPY);
		}
		break ;
        }

        // Create a version of the background DC with opacity already applied
        // for fast screen fill in areas with no text.

        if( backgroundblenddc!=NULL ) DeleteDC(backgroundblenddc) ; backgroundblenddc = CreateCompatibleDC(hdcPrimary);
        if( backgroundblendbm!=NULL ) DeleteObject(backgroundblendbm) ; backgroundblendbm = CreateCompatibleBitmap( hdcPrimary, deskWidth, deskHeight );
        
        DeleteObject(rawImage);
        DeleteDC(bmpdc);
	
	SelectObject(backgroundblenddc, backgroundblendbm);
	BitBlt(
            backgroundblenddc, 0, 0, deskWidth, deskHeight,
            backgrounddc, 0, 0, SRCCOPY
        );
	
	if( conf_get_int(conf,CONF_bg_opacity)/*cfg.bg_opacity*/ >= 0 ) 
		{ color_blend( backgroundblenddc, 0, 0, deskWidth, deskHeight, alphacolor, conf_get_int(conf,CONF_bg_opacity)/*cfg.bg_opacity*/); }
	else 
		{ 
		if( bBgRelToTerm == 1 ) {
			deskWidth = clientWidth ;
			deskHeight = clientHeight ;
			}
		
		color_opacity_gradient( backgroundblenddc, 0, 0, deskWidth, deskHeight, alphacolor, -conf_get_int(conf,CONF_bg_opacity)/*cfg.bg_opacity*/ ); 
		}
    }

    ReleaseDC(hwnd, hdcPrimary);

//DeleteDC(hdcPrimary);

    return TRUE;
}

void paint_term_edges(HDC hdc, LONG paint_left, LONG paint_top, LONG paint_right, LONG paint_bottom) 
{
    if(backgrounddc == 0)
        load_bg_bmp();

    if(backgrounddc)
    {
        LONG topLeftX = paint_left;
        LONG topLeftY = paint_top;
        LONG width = (paint_right - paint_left);
        LONG height = (paint_bottom - paint_top);
        HDC srcdc = backgroundblenddc;
        POINT srcTopLeft;
        RECT size_now;
        srcTopLeft.x = topLeftX;
        srcTopLeft.y = topLeftY;
		
        if(!bBgRelToTerm)
            ClientToScreen(hwnd, &srcTopLeft);

        if(!srcdc)
            srcdc = backgrounddc;

	if(resizing)
	{
	    GetClientRect(hwnd, &size_now);
	    if(size_now.bottom > size_before.bottom || size_now.right > size_before.right)
	    {
	    	// Draw on full area on resize.
	        BitBlt(hdc, topLeftX, topLeftY, width, height, srcdc, srcTopLeft.x, srcTopLeft.y, SRCCOPY);
	        
	        return;
            }
	}
//debug_log("1: %d %d %d %d\n",size_before.top,size_before.bottom,size_before.left,size_before.right) ;
//debug_log("2: %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %d\n",topLeftX,topLeftY,width,height,srcTopLeft.x,srcTopLeft.y,size_now.top,size_now.bottom,size_now.left,size_now.right,resizing) ;
	
        // Draw top edge
        BitBlt(
            hdc, topLeftX, topLeftY, width, return_offset_height(),
            srcdc, srcTopLeft.x, srcTopLeft.y, SRCCOPY
        );
        // Draw left edge
        BitBlt(
            hdc, topLeftX, topLeftY+1, return_offset_width(), height-2,
            srcdc, srcTopLeft.x, srcTopLeft.y+1, SRCCOPY
        );
        // Draw right edge (extra width for clean resizing)
        BitBlt(
            hdc, topLeftX+width-1, topLeftY+1, return_offset_width(), height-2,
            srcdc, srcTopLeft.x+width-1, srcTopLeft.y+1, SRCCOPY
        );
        // Draw bottom edge (extra height for clean resizing)
        BitBlt(
            hdc, topLeftX, topLeftY+height-1, width, return_offset_height(),
            srcdc, srcTopLeft.x, srcTopLeft.y+height-1, SRCCOPY
        );
    }
    else
    {
        HBRUSH bgbrush, oldbrush;
        HPEN edge, oldpen;
        //COLORREF backgroundcolor = colours[258];
	COLORREF backgroundcolor = return_colours258() ;
        
        bgbrush = CreateSolidBrush(backgroundcolor);
        edge = CreatePen(PS_SOLID, 0, backgroundcolor);
        
        oldbrush = SelectObject(hdc, bgbrush);
        oldpen = SelectObject(hdc, edge);

        /*
         * Jordan Russell reports that this apparently
         * ineffectual IntersectClipRect() call masks a
         * Windows NT/2K bug causing strange display
         * problems when the PuTTY window is taller than
         * the primary monitor. It seems harmless enough...
         */
        IntersectClipRect(hdc,
        	paint_left, paint_top,
        	paint_right, paint_bottom);
	
        ExcludeClipRect(hdc, 
        	return_offset_width(), return_offset_height(),
        	return_offset_width()+return_font_width()*(term->cols),
        	return_offset_height()+return_font_height()*(term->rows));

//debug_log("3: %d %d %d %d %d %d %d\n",resizing,paint_left, paint_top, paint_right, paint_bottom,offset_width,offset_height ) ;
//debug_log("4: %d %d %d %d\n",size_before.top,size_before.bottom,size_before.left,size_before.right) ;
//debug_log("5: %d %d %d %d %d %d\n",paint_top,paint_bottom,paint_left,paint_right,term->cols,term->rows);

        Rectangle(hdc, paint_left, paint_top,
            paint_right, paint_bottom);

        SelectObject(hdc, oldpen);
        DeleteObject(edge);
        SelectObject(hdc, oldbrush);
        DeleteObject(bgbrush);
    }
}

/*
void original_paint_term_edges(HDC hdc, LONG paint_left, LONG paint_top, LONG paint_right, LONG paint_bottom) 
{
    if(backgrounddc == 0)
        load_bg_bmp();

    if(backgrounddc)
    {
        LONG topLeftX = paint_left;
        LONG topLeftY = paint_top;
        LONG width = (paint_right - paint_left);
        LONG height = (paint_bottom - paint_top);
        HDC srcdc = backgroundblenddc;
        POINT srcTopLeft;
        RECT size_now;
        srcTopLeft.x = topLeftX;
        srcTopLeft.y = topLeftY;
		
        if(!bBgRelToTerm)
            ClientToScreen(hwnd, &srcTopLeft);

        if(!srcdc)
            srcdc = backgrounddc;

	if(resizing)
	{
	    GetClientRect(hwnd, &size_now);
	    if(size_now.bottom > size_before.bottom || size_now.right > size_before.right)
	    {
	    	// Draw on full area on resize.
	        BitBlt(hdc, topLeftX, topLeftY, width, height, srcdc, srcTopLeft.x, srcTopLeft.y, SRCCOPY);
	        
	        return;
            }
	}
//debug_log("1: %d %d %d %d\n",size_before.top,size_before.bottom,size_before.left,size_before.right) ;
//debug_log("2: %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %d\n",topLeftX,topLeftY,width,height,srcTopLeft.x,srcTopLeft.y,size_now.top,size_now.bottom,size_now.left,size_now.right,resizing) ;
	
        // Draw top edge
        BitBlt(
            hdc, topLeftX, topLeftY, width, offset_height,
            srcdc, srcTopLeft.x, srcTopLeft.y, SRCCOPY
        );
        // Draw left edge
        BitBlt(
            hdc, topLeftX, topLeftY+1, offset_width, height-2,
            srcdc, srcTopLeft.x, srcTopLeft.y+1, SRCCOPY
        );
        // Draw right edge (extra width for clean resizing)
        BitBlt(
            hdc, topLeftX+width-1, topLeftY+1, offset_width, height-2,
            srcdc, srcTopLeft.x+width-1, srcTopLeft.y+1, SRCCOPY
        );
        // Draw bottom edge (extra height for clean resizing)
        BitBlt(
            hdc, topLeftX, topLeftY+height-1, width, offset_height,
            srcdc, srcTopLeft.x, srcTopLeft.y+height-1, SRCCOPY
        );
    }
    else
    {
        HBRUSH bgbrush, oldbrush;
        HPEN edge, oldpen;
        COLORREF backgroundcolor = colours[258];
        
        bgbrush = CreateSolidBrush(backgroundcolor);
        edge = CreatePen(PS_SOLID, 0, backgroundcolor);
        
        oldbrush = SelectObject(hdc, bgbrush);
        oldpen = SelectObject(hdc, edge);

        //
        // * Jordan Russell reports that this apparently
        // * ineffectual IntersectClipRect() call masks a
        // * Windows NT/2K bug causing strange display
        // * problems when the PuTTY window is taller than
        // * the primary monitor. It seems harmless enough...
	
        IntersectClipRect(hdc,
        	paint_left, paint_top,
        	paint_right, paint_bottom);
	
        ExcludeClipRect(hdc, 
        	offset_width, offset_height,
        	offset_width+font_width*(term->cols),
        	offset_height+font_height*(term->rows));

//debug_log("3: %d %d %d %d %d %d %d\n",resizing,paint_left, paint_top, paint_right, paint_bottom,offset_width,offset_height ) ;
//debug_log("4: %d %d %d %d\n",size_before.top,size_before.bottom,size_before.left,size_before.right) ;
//debug_log("5: %d %d %d %d %d %d\n",paint_top,paint_bottom,paint_left,paint_right,term->cols,term->rows);

        Rectangle(hdc, paint_left, paint_top,
            paint_right, paint_bottom);

        SelectObject(hdc, oldpen);
        DeleteObject(edge);
        SelectObject(hdc, oldbrush);
        DeleteObject(bgbrush);
    }
}
*/

void clean_bg(void) {
	DeleteDC(textdc);textdc=NULL;
	DeleteObject(textbm);textbm=NULL;
	//DeleteObject(colorinpixel);
	//DeleteDC(colorinpixeldc);
	//DeleteObject(colorinpixelbm);
	DeleteDC(backgrounddc);backgrounddc=NULL;
	DeleteObject(backgroundbm);backgroundbm=NULL;
	DeleteDC(backgroundblenddc);backgroundblenddc=NULL;
	DeleteObject(backgroundblendbm);backgroundblendbm=NULL;
	}

void RedrawBackground( HWND hwnd ) {
	if(
		1 && // On inhibe cette fonction a cause du probleme de fuite memoire due a l'image de fond !!!  , mais probleme de rafraichissement ?
		(get_param("BACKGROUNDIMAGE"))&&(!get_param("PUTTY"))&&(conf_get_int(conf,CONF_bg_type)/*cfg.bg_type*/ != 0) ) 
			{
			clean_bg() ;
			load_bg_bmp();   // Apparement c'est ça qui faisait la fuite memoire !!!
			}
	RedrawWindow(hwnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
	InvalidateRect(hwnd, NULL, TRUE) ;
	}


#ifdef STARTBUTTON
/*
 * Patch permettant de gerer le probleme de chargement de l'image de fond arrive avec putty 0.61
 */
void BackgroundImagePatch( int num ) {
	int key=0x47 ;  

	if( num == 1 ) { // Double-clic sur une session, on fait ALT-G + Down + ALT-O
		key = 0x47 ; /* VK_G 71 */
		keybd_event(VK_LMENU , 0, 0, 0) ;
		keybd_event( key, 0, 0, 0); 
		keybd_event( key, 0, KEYEVENTF_KEYUP, 0) ;
		keybd_event(VK_LMENU, 0, KEYEVENTF_KEYUP, 0) ;
	
		keybd_event(VK_DOWN, 0, 0, 0) ;
		keybd_event(VK_DOWN, 0, KEYEVENTF_KEYUP, 0) ;

		key=0x4F ;  /* VK_O */
		keybd_event(VK_LMENU , 0, 0, 0) ;
		keybd_event( key, 0, 0, 0); 
		keybd_event( key, 0, KEYEVENTF_KEYUP, 0) ;
		keybd_event(VK_LMENU, 0, KEYEVENTF_KEYUP, 0) ;
		}
	}
#endif

#endif

